#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <span>
#include <vector>

#include "dsp/filter_bank.h"
#include "dsp/iq_block.h"
#include <xtensor/xtensor.hpp>

/**
 * @file
 * The ADS-B demodulator: resample, preamble detection and PPM bit slicing over one block of IQ.
 */

/** One demodulated (not yet validated) frame. */
struct AdsbFrame {
    uint64_t sample_index;      /**< stream sample index (IqBlock::start based) of the preamble */
    unsigned num_bits;          /**< always 112; short replies use the leading 56 */
    unsigned __int128 payload;  /**< bits, first bit received in bit 111 */
    float confidence;           /**< bit-slice magnitude divided by the block RMS */
    int output_index;           /**< preamble position in the block's resampled envelope (AdsbDemod::last_envelope()) */
    float preamble_score;       /**< preamble correlation divided by the block RMS */
};

/**
 * Everything besides the filter taps that fixes what AdsbDemod computes, for reproducing it offline
 * (FrameRecorder). All positions are in the resampled envelope's samples unless noted.
 */
struct AdsbDemodParams {
    double sample_rate_hz;           /**< input rate */
    double output_rate_hz;           /**< envelope rate, samples_per_symbol * 1 Msps */
    int samples_per_symbol;
    int num_bits;                    /**< bits sliced per candidate */
    std::array<float, 16> preamble_pattern;  /**< correlated at preamble_spacing */
    int preamble_spacing;            /**< samples_per_symbol / 2 */
    int bit_offset;                  /**< first bit's position after the preamble position */
    int exclusion_radius;            /**< peak picker's suppression radius around an accepted preamble */
    int max_lookahead;               /**< envelope samples a preamble position needs, itself included */
    float preamble_score_min;        /**< threshold, times the block RMS */
    float slice_magnitude_min;       /**< threshold, times the block RMS */
    int pos_frac_bits;               /**< fixed-point fraction bits of the resampler schedule */
    int64_t pos_step;                /**< schedule: P(m) = m * pos_step + pos_offset */
    int64_t pos_offset;
    int64_t index_offset;            /**< offset giving each output's reported (nearest) input sample */
    int window_lead;                 /**< taps before the center sample in each output's window */
};

/**
 * Demodulates Mode S frames from IQ one block at a time.
 *
 * Each exec() call scans for preambles starting within the block's own samples, and reads the guard
 * padding on either side for the resampler's tap window and for the frame that follows a preamble near the
 * block's end. With the padding filled from the neighboring blocks (BlockStitcher), frames straddling a
 * boundary decode as if the stream were one array. Within a block, every stage runs as a batch-parallel ispc
 * kernel (kernels.ispc) over whole arrays: resample to ADSB_SAMPLES_PER_SYMBOL samples per symbol and take
 * the envelope, correlate the preamble at every position, pick peaks (select_preamble_peaks), and slice 112
 * PPM bits per peak. Thresholds are relative to the block's RMS input level.
 *
 * The only state carried between calls is the peak picker's exclusion zone: a preamble accepted near the
 * end of one block still suppresses candidates within its radius at the start of the next, provided the
 * blocks are contiguous. That makes the peak picking greedy per block rather than over the whole stream,
 * so a boundary candidate is final even if a stronger one follows just past the boundary.
 *
 * @note Known gap: if a preamble sidelobe lands at the end of a block and the true peak just past it, the
 * sidelobe is picked (and fails CRC) and its carried zone suppresses the true peak, losing the frame. This is
 * rare (about 1e-4 per strong frame at 128k-sample blocks). A fix would score a margin of about
 * 8 us + (taps/2)/rate past the block's owned positions, the reach of a sidelobe including filter spread, and
 * let those positions take part in peak selection without being accepted.
 */
class AdsbDemod {
public:
    /**
     * @param filter polyphase filter bank from design_filter_bank() or load_filter_bank(); must outlive the demodulator
     * @param sample_rate_hz input IQ rate
     * @param preamble_score_min preamble correlation threshold, in units of the block RMS
     * @param slice_magnitude_min bit-slice magnitude threshold, in units of the block RMS
     */
    AdsbDemod(const FilterBank& filter, double sample_rate_hz, float preamble_score_min,
              float slice_magnitude_min);

    /**
     * Guard padding exec() needs around each block. The lead covers the resampler's tap window reaching
     * back before sample 0; the trail covers one full frame after a preamble at the block's last sample,
     * plus the tap window, so it grows with the sample rate.
     * @param filter the filter bank the demodulator will use
     * @param sample_rate_hz input IQ rate
     */
    static IqPadding padding(const FilterBank& filter, double sample_rate_hz);

    /** Rate the demodulator resamples its input to (samples per symbol times 1 Msps), for design_filter_bank(). */
    static double output_rate_hz();

    /**
     * Demodulates one block. Not thread-safe: reuses per-instance scratch buffers across calls.
     * @param block at most 2^27 - 1 samples, with at least padding() around them
     * @return frames above both thresholds, in ascending sample order
     * @throws std::length_error if the block is too large
     * @throws std::invalid_argument if the block's padding is smaller than padding()
     */
    std::vector<AdsbFrame> exec(const IqBlock& block);

    /**
     * Stream sample index of every preamble peak the last exec() accepted, before the slice-magnitude
     * threshold (each returned frame's sample_index is one of these). For scoring the preamble scan on its
     * own (apps/adsb_sim_eval).
     */
    const std::vector<uint64_t>& last_candidates() const { return last_candidates_; }

    /** Resampled envelope from the last exec(), outputs [0, n_out); AdsbFrame::output_index indexes it. */
    std::span<const float> last_envelope() const { return {y_.data(), last_n_out_}; }

    /** Block RMS input level from the last exec(), which scales both thresholds. */
    float last_level() const { return last_level_; }

    /** The resampling schedule, constants and thresholds this demodulator runs with. */
    AdsbDemodParams params() const;

private:
    // Scratch reused by every exec() call so none of them allocates or zero-fills; they only ever grow
    // (see grow_scratch in demod.cpp), and each call uses the leading part.
    xt::xtensor<float, 1> y_;               // resampled envelope, one per output sample
    xt::xtensor<float, 1> preamble_score_;  // per scanned position
    std::vector<uint64_t> last_candidates_;

    size_t last_n_out_ = 0;
    float last_level_ = 0;

    const FilterBank& filter_;
    double sample_rate_hz_;
    IqPadding pad_;       // padding(filter_, sample rate)
    int64_t pos_step_;    // resample_block's fixed-point schedule, see demod.cpp constructor
    int64_t pos_offset_;
    int64_t index_offset_;  // for reporting each output's nearest input sample
    float preamble_score_min_;
    float slice_magnitude_min_;

    // Exclusion zone carried to the next block (see exec): valid only for a block starting at next_start_.
    uint64_t next_start_ = 0;
    int next_first_allowed_ = 0;  // first preamble position, in the next block's output samples, not excluded
};
