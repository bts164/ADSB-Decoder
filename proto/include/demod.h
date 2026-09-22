#pragma once

#include <complex>
#include <cstdint>
#include <vector>

#include "filter_bank.h"

#ifndef ADSB_SAMPLES_PER_SYMBOL
#define ADSB_SAMPLES_PER_SYMBOL 12
#endif

struct AdsbFrame {
    uint64_t sample_index;
    unsigned num_bits;
    unsigned __int128 payload;
    float confidence;
    // Sub-chip preamble phase correction (0..ADSB_SAMPLES_PER_SYMBOL-2), in
    // the same y[]-domain sample units as the resample schedule -- kept
    // separate from sample_index (which reports only the coarser m) so a
    // caller can refine the reported index without duplicating demod.cpp's
    // schedule math; see main.cpp's stdout printing for that conversion.
    int ic0;
};

// Required guard-padding around AdsbDemod::exec()'s `x` argument: the
// caller must guarantee adsb_leading_pad(filter.taps) samples of valid
// (zero-filled, in today's whole-file-per-call usage -- a future
// streaming design could instead carry real neighboring-block samples
// there) memory immediately before x[0], and adsb_trailing_pad(filter.taps)
// immediately after x[n-1]. This exists because resample_block's filter-
// tap window reaches taps/2 samples on each side of every output's center
// sample, and its SIMD load additionally overreads a few samples past
// whatever it last needs (see kernels.ispc: resample_task/sumsq_task).
// Depends only on the filter's tap count, not sample rate or block
// length, so a caller can size its buffer once, before any exec() call —
// see main.cpp for how the whole-file loader uses this to read the IQ
// file directly into a pre-padded buffer with no extra copy at all.
int adsb_leading_pad(int filter_taps);
int adsb_trailing_pad(int filter_taps);

// Phase-0 prototype: processes one whole, already-loaded block of IQ
// samples per call (see README.md "Implementation phasing") — there's no
// persistent state across calls yet, that's part of the future streaming
// shared-library design. This lets the whole pipeline (resample, preamble
// scan, bit slice) run as batch-parallel ispc kernels over full arrays
// instead of one ispc call per output sample.
class AdsbDemod {
public:
    // sample_rate_hz: input IQ rate (e.g. 3.2e6). filter: polyphase filter
    // bank loaded via load_filter_bank(). ADSB_SAMPLES_PER_SYMBOL is a
    // compile-time constant (see README.md "Design principle: parameterize
    // everything").
    AdsbDemod(const FilterBank& filter, double sample_rate_hz, float preamble_score_min,
              float slice_magnitude_min);

    // `x` must satisfy the guard-padding contract documented above
    // (adsb_leading_pad/adsb_trailing_pad, sized from this instance's
    // filter bank).
    std::vector<AdsbFrame> exec(const std::complex<float>* x, size_t n);

private:
    const FilterBank& filter_;
    double rx_;  // symbol periods per input sample, see demod.cpp constructor
    float preamble_score_min_;
    float slice_magnitude_min_;
};
