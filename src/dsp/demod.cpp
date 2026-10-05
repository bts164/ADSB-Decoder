#include "dsp/demod.h"
#include "common/cpu_layout.h"
#include "common/log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <span>
#include <stdexcept>
#include <thread>

#include <xtensor/xbuilder.hpp>

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
}  // namespace

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
    : filter_(filter),
      sample_rate_hz_(sample_rate_hz),
      pad_(padding(filter, sample_rate_hz)),
      preamble_score_min_(preamble_score_min),
      slice_magnitude_min_(slice_magnitude_min) {
    // resample_block's schedule, in kernels.ispc's fixed point: output positions advance by ry/rx input samples,
    // offset by pos_offset() (see the derivation there).
    pos_step_ = pos_step(sample_rate_hz);
    pos_offset_ = pos_offset(filter);
    index_offset_ = index_offset(filter);
}

AdsbDemodParams AdsbDemod::params() const {
    AdsbDemodParams p{};
    p.sample_rate_hz = sample_rate_hz_;
    p.output_rate_hz = output_rate_hz();
    p.samples_per_symbol = kS;
    p.num_bits = kNumBits;
    std::copy(std::begin(kPreamblePattern), std::end(kPreamblePattern), p.preamble_pattern.begin());
    p.preamble_spacing = kS / 2;
    p.bit_offset = 8 * kS;
    p.exclusion_radius = kExclusionRadius;
    p.max_lookahead = kMaxLookahead;
    p.preamble_score_min = preamble_score_min_;
    p.slice_magnitude_min = slice_magnitude_min_;
    p.pos_frac_bits = kPosFracBits;
    p.pos_step = pos_step_;
    p.pos_offset = pos_offset_;
    p.index_offset = index_offset_;
    p.window_lead = window_lead(filter_);
    return p;
}

namespace {
// Stage timing, gated by ADSB_TIMING=1 -- ad hoc profiling to attribute
// runtime across stages (see README-adjacent discussion: pipeline-level
// task-parallel speedup on resample_block alone was ~2x despite 12 hw
// threads, which is consistent with either Amdahl's law (resample_block
// being a smaller fraction of total time than assumed) or genuine memory-
// bandwidth saturation from concurrent streaming reads -- this timing is
// how we tell those two apart empirically instead of guessing).
struct StageTimer {
    bool enabled = std::getenv("ADSB_TIMING") != nullptr;
    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    void mark(const char* label) {
        if (!enabled) return;
        auto now = std::chrono::steady_clock::now();
        LOGF(INFO, "[timing] %s: %.3f ms", label, std::chrono::duration<double>(now - t).count() * 1e3);
        t = now;
    }
};
}  // namespace

std::vector<AdsbFrame> AdsbDemod::exec(const IqBlock& block) {
    StageTimer timer;
    last_candidates_.resize({0});
    last_n_out_ = 0;
    last_level_ = 0;
    const int Np = filter_.num_phases;
    const int Mh = window_lead(filter_);
    const size_t n = block.n;

    // ispc has no way to query hardware thread count itself, so the task
    // count for resample_block's and preamble_scan's launches (see
    // kernels.ispc) is chosen here. ADSB_NTASKS overrides it, for isolating
    // a kernel's own single- vs multi-thread scaling (see StageTimer
    // comment above).
    static const unsigned hw_threads = [] {
        if (const char* env = std::getenv("ADSB_NTASKS")) return static_cast<unsigned>(std::atoi(env));
        return adsb::cpu_layout::compute_cpu_count();
    }();

    // kernels.ispc's fixed-point output positions (resample_pos_fixed) cover blocks up to 2^27 samples.
    if (n >= (size_t{1} << 27)) throw std::length_error("AdsbDemod::exec: block too large");
    if (block.lead < pad_.lead || block.trail() < static_cast<size_t>(pad_.trail))
        throw std::invalid_argument("AdsbDemod::exec: block has less guard padding than AdsbDemod::padding()");

    // The exclusion zone from the previous block's last accepted preamble only applies if this block continues
    // straight on from it.
    const int first_allowed = block.start == next_start_ ? next_first_allowed_ : 0;
    next_start_ = block.start + n;
    if (n == 0) return {};
    // Output k sits at input sample (k * step + offset) / 2^36, and the next block's output m at
    // n + (m * step + offset) / 2^36, so k corresponds to the next block's m = k - outputs_per_block.
    const double outputs_per_block =
        static_cast<double>(n) * static_cast<double>(int64_t{1} << kPosFracBits) / static_cast<double>(pos_step_);
    // Unless a pick below reaches further, a carried zone longer than this block carries on into the next.
    next_first_allowed_ = std::max(0, static_cast<int>(std::ceil(first_allowed - outputs_per_block)));

    // --- 1+2. Resample + envelope, batch-parallel (see kernels.ispc: resample_block). This block owns the
    // preamble positions whose center sample is one of its own: outputs [0, scan_n), scan_n being the first
    // output centered at or past sample n, i.e. the least m with m * step + offset >= n << kPosFracBits. The
    // next block starts where that leaves off. Each scanned position also needs kMaxLookahead outputs ahead of
    // it, so the resampled range runs past the block's end into the trailing padding, which padding() sizes
    // for exactly that. Rounding n_out up to whole kOutputBatch batches lets resample_block run without any
    // partial-batch bounds checks; the few extra outputs also read only padding.
    const int64_t first_past = (static_cast<int64_t>(n) << kPosFracBits) - pos_offset_;
    const int scan_n = static_cast<int>((first_past + pos_step_ - 1) / pos_step_);
    const int n_out = (scan_n + kMaxLookahead + kOutputBatch - 1) / kOutputBatch * kOutputBatch;

    // `x` is used directly (interleaved real/imag, matching raw SDR I/Q layout); the block's guard padding
    // makes every index the kernels touch valid. The resampler's tap window for output m reads samples
    // [base - Mh, base - Mh + stride/2), so it takes a pointer to sample -Mh.
    const float* xf = block.iq().data();
    const float* xf_resample = block.padded().subspan(2 * static_cast<size_t>(block.lead - Mh)).data();
    grow_scratch(y_, static_cast<size_t>(n_out));  // every element is written by the kernel
    float* const y = y_.data();
    xt::xtensor<float, 1> partial = xt::zeros<float>({static_cast<size_t>(hw_threads)});
    ispc::resample_block(xf_resample, filter_.kernel_coeffs.data(), filter_.kernel_stride, Np, pos_step_, pos_offset_,
                          n_out, y, xf, static_cast<int>(n), partial.data(), static_cast<int>(hw_threads));
    timer.mark("resample_block");
    last_n_out_ = static_cast<size_t>(n_out);

    // --- RMS normalization, per block, from this block's own samples only (not the padding). resample_block
    // sums the squares as it goes, but y[] stays unnormalized: everything downstream (preamble scores, slice
    // magnitudes) is linear in y, so comparing y/rms against a threshold is the same as comparing y against
    // threshold*rms, which scales two numbers instead of every y.
    float sumsq = 0.0f;
    for (float p : partial) sumsq += p;
    float rms = std::sqrt(sumsq / static_cast<float>(n));
    const float level = rms > 0 ? rms : 1.0f;
    const float inv_level = static_cast<float>(1.0 / level);
    last_level_ = level;

    // --- 3. Preamble correlation at every owned position (see kernels.ispc: preamble_scan).
    grow_scratch(preamble_score_, static_cast<size_t>(scan_n));  // fully written by the kernel
    ispc::preamble_scan(y, scan_n, kS / 2, kPreamblePattern, preamble_score_.data(), static_cast<int>(hw_threads));
    timer.mark("preamble_scan");

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
    // result depends only on preamble_score_[], so accepted candidates are all
    // sliced together below. Positions before first_allowed are still inside
    // the previous block's last pick's zone; dropping them from the scan is the
    // same as having them excluded.
    const int scan_from = std::min(first_allowed, scan_n);
    const xt::xtensor<int, 1> candidates =
        select_preamble_peaks(std::span<const float>(preamble_score_.data(), static_cast<size_t>(scan_n))
                                  .subspan(static_cast<size_t>(scan_from)),
                              preamble_score_min_ * level, kExclusionRadius) +
        scan_from;
    timer.mark("peak-picking");
    if (candidates.size() == 0) return {};

    // Carry the last pick's zone into the next block: it may accept its m only past k + kExclusionRadius.
    next_first_allowed_ =
        std::max(0, static_cast<int>(std::floor(candidates.back() + kExclusionRadius - outputs_per_block)) + 1);

    last_candidates_.resize({candidates.size()});
    for (size_t c = 0; c < candidates.size(); c++) {
        last_candidates_(c) =
            block.start + static_cast<uint64_t>(ispc::resample_center_index(candidates(c), pos_step_, index_offset_));
    }

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
    for (size_t c = 0; c < candidates.size(); c++) {
        if (mag(c) <= slice_magnitude_min_ * level) continue;
        unsigned __int128 payload = 0;
        for (int i = 0; i < kNumBits; i++) {
            payload = (payload << 1) | bits(c, static_cast<size_t>(i));
        }
        AdsbFrame f;
        f.sample_index = last_candidates_(c);
        f.num_bits = kNumBits;
        f.payload = payload;
        f.confidence = mag(c) * inv_level;  // normalized, as if y were y/rms
        f.output_index = candidates(c);
        f.preamble_score = preamble_score_(static_cast<size_t>(candidates(c))) * inv_level;
        results.push_back(f);
    }
    return results;
}
