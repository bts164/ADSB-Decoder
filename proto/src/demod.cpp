#include "demod.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "kernels_ispc.h"

namespace {
// 8-pulse preamble, 16 chips at half-symbol resolution — fixed by the
// Mode S waveform spec, not a tunable (see README.md "Design principle:
// parameterize everything").
const float kPreamblePattern[16] = {1, -1, 1, -1, -1, -1, -1, 1, -1, 1, -1, -1, -1, -1, -1, -1};
constexpr int kS = ADSB_SAMPLES_PER_SYMBOL;
constexpr int kPreamblePhaseCandidates = kS - 2;  // Julia: i in 2:10 for sps=12
constexpr int kNumBits = 112;                     // always sliced; short frames just use the leading 56
// Preamble correlation stays above threshold for a run of several adjacent
// m (the correlation peak has real width, not a single sample), so a
// window this wide is enough to find its local max — matches the Julia
// prototype's own 10-sample search window over the same y[]-rate buffer.
constexpr int kPeakWindow = 10;

// Host-side mirror of the center-sample-index half of kernels.ispc's
// resample_block schedule (see that file for the derivation), used only
// for reporting AdsbFrame::sample_index — must match the kernel's rounding
// convention exactly (std::llround is round-half-away-from-zero, same as
// the kernel's iround, for the non-negative values seen here).
int ix_center_for(int m, double ry, double rx, int Np) {
    double ix_float = static_cast<double>(m) * ry / rx;
    double t = ix_float * Np + Np * 0.5;
    return static_cast<int>(std::llround(t) / Np);
}
}  // namespace

int next_multiple(int n, int k) { return ((n + k - 1) / k) * k; }
int adsb_leading_pad(int filter_taps) { return next_multiple(filter_taps / 2, 64); }
int adsb_trailing_pad(int filter_taps) { return next_multiple(filter_taps / 2 + filter_taps + 16, 64); }

AdsbDemod::AdsbDemod(const FilterBank& filter, double sample_rate_hz, float preamble_score_min,
                      float slice_magnitude_min)
    : filter_(filter), preamble_score_min_(preamble_score_min), slice_magnitude_min_(slice_magnitude_min) {
    double sample_rate_mhz = sample_rate_hz / 1e6;
    rx_ = 1.0 / sample_rate_mhz;
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
        std::cerr << "[timing] " << label << ": " << std::chrono::duration<double>(now - t).count() * 1e3
                  << " ms\n";
        t = now;
    }
};
}  // namespace

std::vector<AdsbFrame> AdsbDemod::exec(const std::complex<float>* x, size_t n) {
    StageTimer timer;
    const double ry = 1.0 / kS;
    const int Np = filter_.num_phases;
    const int taps = filter_.taps;
    const int Mh = taps / 2;

    // ispc has no way to query hardware thread count itself, so the task
    // count for resample_block's and preamble_scan's launches (see
    // kernels.ispc) is chosen here. ADSB_NTASKS overrides it, for isolating
    // a kernel's own single- vs multi-thread scaling (see StageTimer
    // comment above).
    static const unsigned hw_threads = [] {
        if (const char* env = std::getenv("ADSB_NTASKS")) return static_cast<unsigned>(std::atoi(env));
        return std::max(1u, std::thread::hardware_concurrency());
    }();

    // --- 1+2. Resample + envelope, batch-parallel over the whole block
    // (see kernels.ispc: resample_block). The resampling schedule (which
    // input window/filter phase feeds each output sample) is now a closed
    // form of the output index alone — no recurrence, no cross-sample
    // dependency — so every lane in the kernel computes its own schedule
    // entry independently; there's nothing left to precompute here at all.
    // Unlike the streaming prototype, this never skips ahead after a
    // detected frame — every output position gets computed uniformly, and
    // any redundant rescans of an already-decoded frame's payload are
    // filtered out below by the mag/score thresholds instead, same as
    // they already are for the non-redundant case.
    //
    // n_out is an approximate bound, not exact (each input sample feeds
    // ~rx_*kS output steps; the +4 margin covers rounding), because unlike
    // the old serial recurrence there's no longer a natural "ran out of
    // input" stopping point computed as a side effect — a few trailing
    // output samples beyond the real data may draw from the tail's zero
    // padding, same as the leading samples already do from the front's;
    // both are harmless, since they just fail the preamble/slice
    // thresholds below like any other non-signal region.
    const int n_out =
        std::max(0, static_cast<int>(std::ceil(static_cast<double>(n) * rx_ * kS)) + 4);
    if (n_out == 0) return {};

    // `x` is used directly (interleaved real/imag, matching raw SDR I/Q
    // layout) — no memcpy into a padded copy here at all. The caller
    // guarantees the adsb_leading_pad/adsb_trailing_pad guard band around
    // `x` (see demod.h), so every index either kernel below touches is
    // simply valid; an earlier version instead had exec() itself memcpy
    // the whole block into a padded copy every call (O(n), ~7% of total
    // wall time on an 18M-sample file — see README.md's Phase-0 status
    // section), then later split resample into three m-ranges with
    // different backing arrays to cut that down to O(Mh+taps). Moving the
    // padding into the caller's buffer (built once, when it's populated —
    // see main.cpp) gets the same O(Mh+taps) cost with none of that
    // per-call bookkeeping.
    const int n_i = static_cast<int>(n);
    const float* xf = reinterpret_cast<const float*>(x);

    // --- RMS normalization (see README.md "Software-side AGC /
    // normalization"): computed here, per block, from this block's own raw
    // samples only -- no smoothing/state carried across blocks (deliberate:
    // a receiver-side AGC step wouldn't land on a block boundary anyway, so
    // there's no "correct" cross-block-smoothed value to converge on;
    // better to just take the hit on whichever block a real level change
    // lands in and be exactly right on every other block). Folded into
    // resample_block's own output scaling instead of a separate pass over
    // x[] -- see kernels.ispc: sumsq_block / resample_task's `scale`.
    std::vector<float> partial(hw_threads, 0.0);
    ispc::sumsq_block(xf, n_i, partial.data(), static_cast<int>(hw_threads));
    float sumsq = 0.0;
    for (float p : partial) sumsq += p;
    float rms = n > 0 ? std::sqrt(sumsq / static_cast<float>(n)) : 0.0;
    float scale = rms > 0 ? static_cast<float>(1.0 / rms) : 1.0f;
    timer.mark("rms (reduction)");

    // --- Resample + envelope over the whole [0, n_out) range in one call —
    // see kernels.ispc's resample_task/resample_block comment for `base`.
    // `base` (== ix_center) is a raw sample index, and the kernel's tap
    // window reads `x[base+k]` for k in [0, taps), centered so k=Mh lands
    // on `base` itself — i.e. it expects index 0 of its `x` argument to be
    // raw sample -Mh, not raw sample 0. `xf_resample` supplies exactly
    // that (valid because of the adsb_leading_pad guard band before x[0]).
    const float* xf_resample = xf - 2 * Mh;
    std::vector<float> y(static_cast<size_t>(n_out));
    ispc::resample_block(xf_resample, filter_.coeffs.data(), taps, Np, Mh, rx_, ry, n_out, scale, y.data(),
                          static_cast<int>(hw_threads));
    timer.mark("resample_block");

    // --- 3. Preamble correlation, batch-parallel over every candidate
    // start position (see kernels.ispc: preamble_scan). scan_limit leaves
    // enough of y[] ahead of the last scanned position for both this
    // kernel (needs 99 samples) and the bit slice below (needs up to
    // ~120*kS samples).
    const int max_lookahead = (kPreamblePhaseCandidates - 1) + 8 * kS + (kNumBits - 1) * kS + kS / 2 + 1;
    const int scan_limit = std::max(0, n_out - max_lookahead);
    if (scan_limit == 0) return {};

    std::vector<float> best_score(static_cast<size_t>(scan_limit));
    std::vector<int> best_ic(static_cast<size_t>(scan_limit));
    ispc::preamble_scan(y.data(), scan_limit, kPreamblePhaseCandidates, kPreamblePattern, best_ic.data(),
                         best_score.data(), static_cast<int>(hw_threads));
    timer.mark("preamble_scan");

    // --- 4. Host-side peak-picking: unlike the streaming Julia prototype,
    // every output position gets a preamble score computed uniformly (see
    // step 1+2's comment), so a single real preamble produces a whole run
    // of adjacent m whose score clears the threshold — one candidate per m
    // would demodulate the same physical signal 10-15 times. Mirrors what
    // the Julia prototype did instead: find the local max within a small
    // window once threshold is crossed, emit only that one, then skip
    // ahead past the full frame span so nothing else within this signal's
    // own duration (preamble sidelobes, bit-pattern coincidences) gets
    // picked up as a separate detection.
    struct Candidate {
        int m;
        int ic0;
    };
    std::vector<Candidate> candidates;
    for (int m = 0; m < scan_limit;) {
        if (best_score[static_cast<size_t>(m)] < preamble_score_min_) {
            m++;
            continue;
        }
        int peak_m = m;
        float peak_score = best_score[static_cast<size_t>(m)];
        int window_end = std::min(scan_limit, m + kPeakWindow);
        for (int j = m + 1; j < window_end; j++) {
            if (best_score[static_cast<size_t>(j)] > peak_score) {
                peak_score = best_score[static_cast<size_t>(j)];
                peak_m = j;
            }
        }
        candidates.push_back({peak_m, best_ic[static_cast<size_t>(peak_m)]});
        m = peak_m + max_lookahead;  // skip the rest of this signal's frame span
    }
    timer.mark("peak-picking");
    if (candidates.empty()) return {};

    std::vector<int> starts(candidates.size());
    for (size_t c = 0; c < candidates.size(); c++) {
        starts[c] = candidates[c].m + candidates[c].ic0 + 8 * kS;
    }

    // --- 5. PPM bit slicing, batch-parallel over just the filtered
    // candidates (see kernels.ispc: slice_scan).
    std::vector<uint8_t> bits(candidates.size() * kNumBits);
    std::vector<float> mag(candidates.size());
    ispc::slice_scan(y.data(), starts.data(), kS, kS / 2, kNumBits, static_cast<int>(candidates.size()), bits.data(),
                      mag.data());
    timer.mark("slice_scan");

    std::vector<AdsbFrame> results;
    for (size_t c = 0; c < candidates.size(); c++) {
        if (mag[c] <= slice_magnitude_min_) continue;
        unsigned __int128 payload = 0;
        for (int i = 0; i < kNumBits; i++) {
            payload = (payload << 1) | bits[c * kNumBits + static_cast<size_t>(i)];
        }
        AdsbFrame f;
        f.sample_index = static_cast<uint64_t>(ix_center_for(candidates[c].m, ry, rx_, Np));
        f.num_bits = kNumBits;
        f.payload = payload;
        f.confidence = mag[c];
        f.ic0 = candidates[c].ic0;
        results.push_back(f);
    }
    return results;
}
