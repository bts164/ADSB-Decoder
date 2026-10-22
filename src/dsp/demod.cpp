#include "dsp/demod.h"
#include "common/log.h"
#include "common/trace.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <span>
#include <string_view>
#include <stdexcept>
#include <thread>
#include <vector>

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/runtime/runtime.h>
#include <coro/task/parallel.h>
#include <xtensor/xbuilder.hpp>
#include <xtensor/xtensor.hpp>

#include "kernels_ispc.h"
#include "dsp/peak_select.h"

#ifndef ADSB_SAMPLES_PER_SYMBOL
#define ADSB_SAMPLES_PER_SYMBOL 12
#endif

namespace {
// 8-pulse preamble, 16 chips at half-symbol resolution — fixed by the
// Mode S waveform spec, not a tunable (see README.md "Design principle:
// parameterize everything").
const float kPreamblePattern[16] = {1, -1, 1, -1, -1, -1, -1, 1, -1, 1, -1, -1, -1, -1, -1, -1};
constexpr int kS = ADSB_SAMPLES_PER_SYMBOL;
constexpr int kNumBits = 112;                     // always sliced; short frames just use the leading 56
// Two frames whose preambles start closer than this overlap on the air (8 us
// preamble + 112 us message, kS y-samples per us), so once a preamble peak is
// accepted nothing within this distance of it, either side, can be an
// independent detection -- it is either a ripple of the preamble correlation,
// a coincidental match in the message pulses, or noise.
constexpr int kExclusionRadius = (8 + kNumBits) * kS;
}  // namespace

namespace {
// Makes `buf` hold at least n elements without initializing them (xtensor's resize reallocates on any size
// change, so only grow).
template <class T>
void grow_scratch(xt::xtensor<T, 1>& buf, size_t n) {
    if (buf.size() < n) buf = xt::xtensor<T, 1>::from_shape({n});
}

// Outputs a preamble at output k needs ahead of it: y[k .. k + kMaxLookahead) covers the preamble
// correlation (15 * kS/2 + 1) and the bit slice of a full frame (8 us preamble, then 112 bits sampled at
// start + i*kS and start + i*kS + kS/2).
constexpr int kMaxLookahead = 8 * kS + (kNumBits - 1) * kS + kS / 2 + 1;
// resample_block computes whole batches of up to this many outputs (the widest ispc gang), so the output
// count is rounded up to a multiple of it.
constexpr int kOutputBatch = 64;
constexpr int kPosFracBits = 36;  // kernels.ispc's fixed-point position format (resample_pos_fixed)

int64_t pos_step(double sample_rate_hz) { return ispc::resample_pos_fixed((1.0 / kS) * (sample_rate_hz / 1e6)); }
// Taps before the center sample `base` in the resampler's window [base - window_lead, base - window_lead + taps).
// With odd taps the window is centered on base; with even taps, on base + 1/2, which pos_offset compensates for.
int window_lead(const FilterBank& filter) { return (filter.taps - 1) / 2; }
// Output 0's position: 1/(2*Np) centers each phase's sub-sample interval, plus 1/2 for odd taps so base is the
// nearest sample (polyphase.jl's `Np÷2` start) -- even taps get that half sample from their window instead.
// Either way output m falls at the same input time, about m * ry/rx.
int64_t pos_offset(const FilterBank& filter) {
    return ispc::resample_pos_fixed((filter.taps % 2 == 1 ? 0.5 : 0.0) + 0.5 / filter.num_phases);
}
// The offset that makes resample_center_index() the input sample nearest each output, for any tap count
// (pos_offset for odd taps).
int64_t index_offset(const FilterBank& filter) { return ispc::resample_pos_fixed(0.5 + 0.5 / filter.num_phases); }

// The resampler kernel for blocks in format F (kernels.ispc: RESAMPLE_FORMAT), which computes one part's share.
// This is the one place that pairs each of iq::Formats with its kernel; a format without a specialization here
// fails to compile where exec() visits the block.
template <class F>
struct ResampleKernel;
template <>
struct ResampleKernel<iq::F32> {
    static constexpr auto chunk = &ispc::resample_chunk_f32;
};
template <>
struct ResampleKernel<iq::I16Be> {
    static constexpr auto chunk = &ispc::resample_chunk_i16be;
};

// The samples are used directly, in the block's own format (interleaved real/imag, matching raw SDR I/Q
// layout), by that format's kernel; the block's guard padding makes every index the kernels touch valid.
// The resampler's tap window for output m reads samples [base - Mh, base - Mh + stride/2), so it takes a
// pointer to sample -Mh. The kernel scales its outputs by 1 / full_scale, so y[] and the sum of squares
// are the same as for input scaled to [-1, 1], whatever the format.
template <class F>
auto tap_window_base(IqView<F> v, int Mh) {
    return v.padded.subspan(2 * static_cast<size_t>(v.lead - Mh)).data();
}

// demodulate()'s state (see demod.h): demodulates one block per exec() call, carrying the peak picker's
// exclusion zone from each block to the next.
class AdsbDemod {
public:
    // `filter` must outlive the demodulator. The thresholds are in units of the block RMS.
    AdsbDemod(const FilterBank& filter, double sample_rate_hz, float preamble_score_min,
              float slice_magnitude_min);

    // See demod_padding().
    static IqPadding padding(const FilterBank& filter, double sample_rate_hz);
    // See demod_output_rate_hz().
    static double output_rate_hz();

    // Demodulates one block, which must outlive the returned coroutine: frames above both thresholds, in
    // ascending sample order. One call at a time: every call reuses the same scratch buffers.
    [[nodiscard]] coro::Coro<std::vector<AdsbFrame>> exec(const IqBlock& block);

    // From the last exec(): each accepted preamble peak's stream sample index, before the slice threshold.
    const xt::xtensor<uint64_t, 1>& last_candidates() const { return m_last_candidates; }
    // From the last exec(): the resampled envelope, outputs [0, n_out).
    std::span<const float> last_envelope() const { return {y_.data(), m_last_n_out}; }
    // From the last exec(): the envelope outputs the block owns, [0, scan_n), and the envelope stream index of
    // the first (see DemodBlock::envelope_index).
    std::span<const float> last_owned_envelope() const { return {y_.data(), m_last_scan_n}; }
    uint64_t last_envelope_index() const { return m_last_envelope_index; }
    // From the last exec(): the block's RMS envelope level.
    float last_level() const { return m_last_level; }

    AdsbDemodParams params() const;

private:
    // What exec() works out for a block before the kernels run (see begin_block).
    struct BlockPlan {
        int first_allowed;          // first preamble position not excluded by the previous block's last pick
        double outputs_per_block;   // resampled outputs per block sample count, for carrying the exclusion zone
        int scan_n;                 // preamble positions this block owns: outputs [0, scan_n)
        int n_out;                  // outputs to resample, a multiple of the widest gang
    };
    // Checks the block, carries the exclusion zone and sizes the scratch for the kernels; nullopt for an
    // empty block, which has nothing to scan.
    std::optional<BlockPlan> begin_block(const IqBlock& block);
    // Everything after the kernels: the RMS level from their partial sums of squares, peak picking, bit
    // slicing and the frames.
    std::vector<AdsbFrame> finish_block(const IqBlock& block, const BlockPlan& plan, std::span<const float> sumsq);

    // Scratch reused by every exec() call so none of them allocates or zero-fills; they only ever grow
    // (see grow_scratch in demod.cpp), and each call uses the leading part.
    xt::xtensor<float, 1> y_;               // resampled envelope, one per output sample
    xt::xtensor<float, 1> m_preamble_score;  // per scanned position
    xt::xtensor<uint64_t, 1> m_last_candidates;

    size_t m_last_n_out = 0;
    size_t m_last_scan_n = 0;
    uint64_t m_last_envelope_index = 0;
    float m_last_level = 0;

    const FilterBank& m_filter;
    double m_sample_rate_hz;
    IqPadding m_pad;       // padding(m_filter, sample rate)
    int64_t m_pos_step;    // resample_block's fixed-point schedule, see demod.cpp constructor
    int64_t m_pos_offset;
    int64_t m_index_offset;  // for reporting each output's nearest input sample
    float m_preamble_score_min;
    float m_slice_magnitude_min;

    // Exclusion zone carried to the next block (see exec): valid only for a block starting at m_next_start.
    uint64_t m_next_start = 0;
    int m_next_first_allowed = 0;  // first preamble position, in the next block's output samples, not excluded
};

double AdsbDemod::output_rate_hz() { return kS * 1e6; }

IqPadding AdsbDemod::padding(const FilterBank& filter, double sample_rate_hz) {
    // exec() resamples outputs [0, n_out), where n_out = round_up(scan_n + kMaxLookahead, kOutputBatch) and
    // scan_n is the first output centered at or past sample n. Output m reads samples [base - Mh, base - Mh +
    // stride/2), Mh = window_lead(), base = floor(P(m)), P(m) = m * step + offset. base(0) = 0 (offset < 1), so
    // the lead is Mh.
    // P(scan_n - 1) < n, so P(n_out - 1) < n + (kMaxLookahead + kOutputBatch - 1) * step: the last sample read
    // is at most n - 1 + ceil(that * step) - Mh + stride/2 - 1, and one sample of slack covers the rounding.
    const int Mh = window_lead(filter);
    const double step = static_cast<double>(pos_step(sample_rate_hz)) / static_cast<double>(int64_t{1} << kPosFracBits);
    const int reach = static_cast<int>(std::ceil((kMaxLookahead + kOutputBatch - 1) * step));
    return {Mh, reach + filter.kernel_stride / 2 - Mh};
}

AdsbDemod::AdsbDemod(const FilterBank& filter, double sample_rate_hz, float preamble_score_min,
                      float slice_magnitude_min)
    : m_filter(filter),
      m_sample_rate_hz(sample_rate_hz),
      m_pad(padding(filter, sample_rate_hz)),
      m_preamble_score_min(preamble_score_min),
      m_slice_magnitude_min(slice_magnitude_min) {
    // resample_block's schedule, in kernels.ispc's fixed point: output positions advance by ry/rx input samples,
    // offset by pos_offset() (see the derivation there).
    m_pos_step = pos_step(sample_rate_hz);
    m_pos_offset = pos_offset(filter);
    m_index_offset = index_offset(filter);
}

AdsbDemodParams AdsbDemod::params() const {
    AdsbDemodParams p{};
    p.sample_rate_hz = m_sample_rate_hz;
    p.output_rate_hz = output_rate_hz();
    p.samples_per_symbol = kS;
    p.num_bits = kNumBits;
    std::copy(std::begin(kPreamblePattern), std::end(kPreamblePattern), p.preamble_pattern.begin());
    p.preamble_spacing = kS / 2;
    p.bit_offset = 8 * kS;
    p.exclusion_radius = kExclusionRadius;
    p.max_lookahead = kMaxLookahead;
    p.preamble_score_min = m_preamble_score_min;
    p.slice_magnitude_min = m_slice_magnitude_min;
    p.pos_frac_bits = kPosFracBits;
    p.pos_step = m_pos_step;
    p.pos_offset = m_pos_offset;
    p.index_offset = m_index_offset;
    p.window_lead = window_lead(m_filter);
    return p;
}

// ADSB_TIMING, set to anything but 0 (as for run_demod_loop's report): read once, at the first block, and logged then.
bool stage_timing_enabled() {
    static const bool enabled = [] {
        const char* env = std::getenv("ADSB_TIMING");
        if (!env) return false;
        const bool on = std::string_view(env) != "0";
        LOGF(INFO, "ADSB_TIMING=%s: %s", env, on ? "logging the demodulator's stage times for every block"
                                                 : "the demodulator's stage times are off");
        return on;
    }();
    return enabled;
}

// Wall time per stage of each block, with ADSB_TIMING set. Tells a stage that doesn't scale with the part
// count because it is a small share of the block (Amdahl) from one that is memory-bandwidth bound.
struct StageTimer {
    bool enabled = stage_timing_enabled();
    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    void mark(const char* label) {
        if (!enabled) return;
        auto now = std::chrono::steady_clock::now();
        LOGF(INFO, "[timing] %s: %.3f ms", label, std::chrono::duration<double>(now - t).count() * 1e3);
        t = now;
    }
};

std::optional<AdsbDemod::BlockPlan> AdsbDemod::begin_block(const IqBlock& block) {
    m_last_candidates.resize({0});
    m_last_n_out = 0;
    m_last_level = 0;
    const size_t n = block.n();

    // kernels.ispc's fixed-point output positions (resample_pos_fixed) cover blocks up to 2^27 samples.
    if (n >= (size_t{1} << 27)) throw std::length_error("demodulate: block too large");
    if (block.lead() < m_pad.lead || block.trail() < static_cast<size_t>(m_pad.trail))
        throw std::invalid_argument("demodulate: block has less guard padding than demod_padding()");

    BlockPlan plan;
    // The exclusion zone from the previous block's last accepted preamble only applies if this block continues
    // straight on from it.
    plan.first_allowed = block.start() == m_next_start ? m_next_first_allowed : 0;
    m_last_envelope_index = block.start() == m_next_start
                                ? m_last_envelope_index + m_last_scan_n
                                : static_cast<uint64_t>(std::llround(static_cast<double>(block.start()) *
                                                                     output_rate_hz() / m_sample_rate_hz));
    m_last_scan_n = 0;
    m_next_start = block.start() + n;
    if (n == 0) return std::nullopt;
    // Output k sits at input sample (k * step + offset) / 2^36, and the next block's output m at
    // n + (m * step + offset) / 2^36, so k corresponds to the next block's m = k - outputs_per_block.
    plan.outputs_per_block =
        static_cast<double>(n) * static_cast<double>(int64_t{1} << kPosFracBits) / static_cast<double>(m_pos_step);
    // Unless a pick in finish_block reaches further, a carried zone longer than this block carries on into the next.
    m_next_first_allowed = std::max(0, static_cast<int>(std::ceil(plan.first_allowed - plan.outputs_per_block)));

    // --- 1+2. Resample + envelope, batch-parallel (see kernels.ispc: resample_block). This block owns the
    // preamble positions whose center sample is one of its own: outputs [0, scan_n), scan_n being the first
    // output centered at or past sample n, i.e. the least m with m * step + offset >= n << kPosFracBits. The
    // next block starts where that leaves off. Each scanned position also needs kMaxLookahead outputs ahead of
    // it, so the resampled range runs past the block's end into the trailing padding, which padding() sizes
    // for exactly that. Rounding n_out up to whole kOutputBatch batches lets resample_block run without any
    // partial-batch bounds checks; the few extra outputs also read only padding.
    const int64_t first_past = (static_cast<int64_t>(n) << kPosFracBits) - m_pos_offset;
    plan.scan_n = static_cast<int>((first_past + m_pos_step - 1) / m_pos_step);
    plan.n_out = (plan.scan_n + kMaxLookahead + kOutputBatch - 1) / kOutputBatch * kOutputBatch;

    grow_scratch(y_, static_cast<size_t>(plan.n_out));               // every element is written by the resampler
    grow_scratch(m_preamble_score, static_cast<size_t>(plan.scan_n));  // and by the preamble correlation
    m_last_n_out = static_cast<size_t>(plan.n_out);
    m_last_scan_n = static_cast<size_t>(plan.scan_n);
    return plan;
}

std::vector<AdsbFrame> AdsbDemod::finish_block(const IqBlock& block, const BlockPlan& plan,
                                               std::span<const float> sumsq_partial) {
    StageTimer timer;
    const float* const y = y_.data();
    const int scan_n = plan.scan_n;

    // --- RMS normalization, per block, from the envelope outputs the block owns (not those over the padding),
    // so after the filter (see kernels.ispc: RESAMPLE_FORMAT). resample_block sums the squares as it goes, but
    // y[] stays unnormalized: everything downstream (preamble scores, slice
    // magnitudes) is linear in y, so comparing y/rms against a threshold is the same as comparing y against
    // threshold*rms, which scales two numbers instead of every y.
    float sumsq = 0.0f;
    for (float p : sumsq_partial) sumsq += p;
    float rms = std::sqrt(sumsq / static_cast<float>(scan_n));
    const float level = rms > 0 ? rms : 1.0f;
    const float inv_level = static_cast<float>(1.0 / level);
    m_last_level = level;

    // --- 4. Host-side peak selection: every output position gets a preamble
    // score, so one real preamble produces a cluster
    // of above-threshold m, including ripples of the correlation well away
    // from the true peak (the preamble's own pulse pattern makes sidelobes a
    // few us to either side). A threshold crossing or a small local window
    // can therefore land on a ripple and misalign the bit slicer. Instead,
    // take the strongest remaining candidate over the whole block, accept it,
    // and discard everything within kExclusionRadius of it (a real frame
    // occupies that span whether or not it later validates, so nothing there
    // is an independent detection). Repeat until no candidates remain. The
    // result depends only on m_preamble_score[], so accepted candidates are all
    // sliced together below. Positions before first_allowed are still inside
    // the previous block's last pick's zone; dropping them from the scan is the
    // same as having them excluded.
    auto candidates = [&]() {
        TRACE_ZONE("select_preamble_peaks");
        const int scan_from = std::min(plan.first_allowed, scan_n);
        const xt::xtensor<int, 1> candidates =
            select_preamble_peaks(std::span<const float>(m_preamble_score.data(), static_cast<size_t>(scan_n))
                                    .subspan(static_cast<size_t>(scan_from)),
                                m_preamble_score_min * level, kExclusionRadius) +
            scan_from;
        timer.mark("peak-picking");
        if (candidates.size() == 0) return candidates;  // no candidates, nothing to carry into the next block

        // Carry the last pick's zone into the next block: it may accept its m only past k + kExclusionRadius.
        m_next_first_allowed = std::max(
            0, static_cast<int>(std::floor(candidates.back() + kExclusionRadius - plan.outputs_per_block)) + 1);

        m_last_candidates.resize({candidates.size()});
        for (size_t c = 0; c < candidates.size(); c++) {
            m_last_candidates(c) = block.start() + static_cast<uint64_t>(
                ispc::resample_center_index(candidates(c), m_pos_step, m_index_offset));
        }
        return candidates;
    }();
    if (candidates.size() == 0) return {};

    const xt::xtensor<int, 1> starts = candidates + 8 * kS;

    // --- 5. PPM bit slicing, batch-parallel over just the filtered
    // candidates (see kernels.ispc: slice_scan).
    // One row of bits per candidate. Both are fully written by the kernel.
    const std::array<size_t, 2> bits_shape{candidates.size(), static_cast<size_t>(kNumBits)};
    xt::xtensor<uint8_t, 2> bits = xt::empty<uint8_t>(bits_shape);
    xt::xtensor<float, 1> mag = xt::empty<float>(std::array<size_t, 1>{candidates.size()});
    ispc::slice_scan(y, starts.data(), kS, kS / 2, kNumBits, static_cast<int>(candidates.size()), bits.data(),
                      mag.data());
    timer.mark("slice_scan");

    std::vector<AdsbFrame> results;
    TRACE_ZONE("assemble_frames");
    for (size_t c = 0; c < candidates.size(); c++) {
        if (mag(c) <= m_slice_magnitude_min * level) continue;
        unsigned __int128 payload = 0;
        for (int i = 0; i < kNumBits; i++) {
            payload = (payload << 1) | bits(c, static_cast<size_t>(i));
        }
        AdsbFrame f;
        f.sample_index = m_last_candidates(c);
        f.num_bits = kNumBits;
        f.payload = payload;
        f.confidence = mag(c) * inv_level;  // normalized, as if y were y/rms
        f.output_index = candidates(c);
        f.preamble_score = m_preamble_score(static_cast<size_t>(candidates(c))) * inv_level;
        results.push_back(f);
    }
    return results;
}

coro::Coro<std::vector<AdsbFrame>> AdsbDemod::exec(const IqBlock& block) {
    std::optional<BlockPlan> plan;
    {
        TRACE_ZONE("demod_begin");
        plan = begin_block(block);
    }
    if (!plan) co_return std::vector<AdsbFrame>{};

    // One part per worker, so that every worker that is idle takes one. ADSB_NTASKS overrides it, for
    // isolating the kernels' own single- vs multi-thread scaling (see StageTimer). resample_chunk needs at
    // most n_out parts; preamble_chunk gives any parts past scan_n nothing to do.
    static const size_t ntasks_override = [] {
        const char* env = std::getenv("ADSB_NTASKS");
        if (!env) return size_t{0};
        char* end = nullptr;
        const unsigned long n = std::strtoul(env, &end, 10);
        if (end == env || *end != '\0' || n == 0) {
            LOGF(WARNING, "ignoring ADSB_NTASKS=%s: not a positive part count; the demodulator uses one part per "
                 "worker", env);
            return size_t{0};
        }
        LOGF(INFO, "ADSB_NTASKS=%s: the demodulator splits each block into %lu parts instead of one per worker "
             "(%zu)", env, n, coro::worker_count());
        return static_cast<size_t>(n);
    }();
    const size_t k = std::clamp(ntasks_override ? ntasks_override : coro::worker_count(), size_t{1},
                                static_cast<size_t>(plan->n_out));
    std::vector<float> sumsq(k, 0.0f);  // each part's sum of squares of the block's envelope
    StageTimer timer;
    // Each part resamples its share of the outputs, then, once every part has written its share, correlates
    // the preamble over its share of the positions, which reads up to 15 * kS/2 outputs past its own range.
    co_await coro::parallel(k, [&](coro::TeamMember part) -> coro::Coro<void> {
        const int index = static_cast<int>(part.rank());
        const int count = static_cast<int>(part.size());
        block.visit([&]<class F>(IqView<F> v) {
            TRACE_ZONE("resample_chunk");
            ResampleKernel<F>::chunk(tap_window_base(v, window_lead(m_filter)), m_filter.kernel_coeffs.data(),
                                     m_filter.kernel_stride, m_filter.num_phases, m_pos_step, m_pos_offset, plan->n_out,
                                     y_.data(), plan->scan_n, 1.0f / F::full_scale, sumsq.data(), index, count);
        });
        co_await part.sync();
        TRACE_ZONE("preamble_chunk");
        ispc::preamble_chunk(y_.data(), plan->scan_n, kS / 2, kPreamblePattern, m_preamble_score.data(), index,
                             count);
    });
    timer.mark("resample_block + preamble_scan");

    TRACE_ZONE("demod_finish");
    co_return finish_block(block, *plan, sumsq);
}

// One block's DemodBlock, with a snapshot of it when `snapshot` is set and it has frames, and its envelope when
// `envelope` is set.
coro::Coro<DemodBlock> demod_block(AdsbDemod& demod, IqBlock block, bool snapshot, bool envelope) {
    DemodBlock out{.n = block.n(),
                   .frames = co_await demod.exec(block),
                   .candidates = {},
                   .snapshot = std::nullopt,
                   .envelope = {}};
    out.candidates.assign(demod.last_candidates().begin(), demod.last_candidates().end());
    if (envelope) {
        const std::span<const float> owned = demod.last_owned_envelope();
        out.envelope.assign(owned.begin(), owned.end());
        out.envelope_index = demod.last_envelope_index();
    }
    if (snapshot && !out.frames.empty()) {
        const std::span<const float> envelope = demod.last_envelope();
        out.snapshot = DemodSnapshot{std::move(block), {envelope.begin(), envelope.end()}, demod.last_level()};
    }
    co_return out;
}

}  // namespace

IqPadding demod_padding(const FilterBank& filter, double sample_rate_hz) {
    return AdsbDemod::padding(filter, sample_rate_hz);
}

double demod_output_rate_hz() { return AdsbDemod::output_rate_hz(); }

AdsbDemodParams demod_params(const FilterBank& filter, double sample_rate_hz, float preamble_score_min,
                             float slice_magnitude_min) {
    return AdsbDemod(filter, sample_rate_hz, preamble_score_min, slice_magnitude_min).params();
}

coro::CoroStream<DemodBlock> demodulate(coro::CoroStream<IqBlock> in, FilterBank filter, double sample_rate_hz,
                                        float preamble_score_min, float slice_magnitude_min, bool snapshots,
                                        std::optional<coro::WatchReceiver<bool>> envelopes) {
    AdsbDemod demod(filter, sample_rate_hz, preamble_score_min, slice_magnitude_min);
    while (auto block = co_await coro::next(in)) {
        const bool envelope = envelopes && envelopes->get();
        co_yield co_await demod_block(demod, std::move(*block), snapshots, envelope);
    }
}
