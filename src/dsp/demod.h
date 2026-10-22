#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <coro/coro_stream.h>
#include <coro/sync/watch.h>

#include "dsp/filter_bank.h"
#include "dsp/iq_block.h"

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
    int output_index;           /**< preamble position in the block's resampled envelope (DemodSnapshot::envelope) */
    float preamble_score;       /**< preamble correlation divided by the block RMS */
};

/**
 * Everything besides the filter taps that fixes what demodulate() computes, for reproducing it offline
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
 * Guard padding demodulate() needs around each input block. The lead covers the resampler's tap window
 * reaching back before sample 0; the trail covers one full frame after a preamble at the block's last sample,
 * plus the tap window, so it grows with the sample rate.
 * @param filter the filter bank demodulate() will use
 * @param sample_rate_hz input IQ rate
 */
IqPadding demod_padding(const FilterBank& filter, double sample_rate_hz);

/** Rate demodulate() resamples its input to (samples per symbol times 1 Msps), for design_filter_bank(). */
double demod_output_rate_hz();

/** The resampling schedule, constants and thresholds demodulate() runs with these settings (FrameRecorder). */
AdsbDemodParams demod_params(const FilterBank& filter, double sample_rate_hz, float preamble_score_min,
                             float slice_magnitude_min);

/** What the demodulator read from one block, for recording it (FrameRecorder). */
struct DemodSnapshot {
    IqBlock block;                /**< the block itself, its padding holding its neighbors' samples */
    std::vector<float> envelope;  /**< resampled envelope, which AdsbFrame::output_index indexes */
    float level = 0;              /**< block RMS envelope level, which scales both thresholds */
};

/** demodulate()'s output for one input block. */
struct DemodBlock {
    size_t n = 0;                           /**< the block's sample count */
    std::vector<AdsbFrame> frames;          /**< frames above both thresholds, in ascending sample order */
    /** Stream sample index of every preamble peak accepted, before the slice-magnitude threshold (each frame's
     *  sample_index is one of these). For scoring the preamble scan on its own (apps/adsb_sim_eval). */
    std::vector<uint64_t> candidates;
    std::optional<DemodSnapshot> snapshot;  /**< with `snapshots`, for a block with frames */
    /** While `envelopes` holds true: the resampled envelope outputs the block owns (those centered on its own samples).
     *  While the blocks are contiguous each block's follow on from the last's, as one stream at
     *  demod_output_rate_hz(), but for an output position jumping by under one output at each boundary. */
    std::vector<float> envelope;
    /** With an envelope: the envelope stream index of envelope[0]. After a gap in the blocks it restarts at the
     *  block's start converted to the output rate. */
    uint64_t envelope_index = 0;
};

/**
 * Demodulates Mode S frames from a stream of IQ blocks, one output per block, in order.
 *
 * In each block it scans for preambles starting within the block's own samples, and reads the padding on
 * either side, which stitch() filled from its neighbors, for the resampler's tap window and for the frame that
 * follows a preamble near the block's end, so frames straddling a boundary decode as if the stream were one
 * array. Every stage runs as a batch-parallel ispc kernel (kernels.ispc) over whole arrays: resample
 * to ADSB_SAMPLES_PER_SYMBOL samples per symbol and take the envelope, correlate the preamble at every
 * position, pick peaks (select_preamble_peaks), and slice 112 PPM bits per peak. Thresholds are relative to
 * the block's RMS envelope level, taken after the resampling filter, so they are relative to the noise in the
 * band the filter passes, however wide the input is.
 *
 * The resampler and the preamble correlation are also split across the runtime's workers: one
 * coro::parallel() call, a part per worker (ADSB_NTASKS overrides the count), with a sync() of their team
 * between them. So this belongs on a multi-threaded runtime; on a single-threaded one the parts run one after
 * another.
 *
 * The only state carried from block to block is the peak picker's exclusion zone: a preamble accepted near the
 * end of one block still suppresses candidates within its radius at the start of the next, provided the
 * blocks are contiguous. That makes the peak picking greedy per block rather than over the whole stream,
 * so a boundary candidate is final even if a stronger one follows just past the boundary.
 *
 * @note Known gap: if a preamble sidelobe lands at the end of a block and the true peak just past it, the
 * sidelobe is picked (and fails CRC) and its carried zone suppresses the true peak, losing the frame. This is
 * rare (about 1e-4 per strong frame at 128k-sample blocks). A fix would score a margin of about
 * 8 us + (taps/2)/rate past the block's owned positions, the reach of a sidelobe including filter spread, and
 * let those positions take part in peak selection without being accepted.
 * @param in blocks from stitch(), of at most 2^27 - 1 samples, each with at least demod_padding() around them
 * @param filter polyphase filter bank from design_filter_bank() or load_filter_bank()
 * @param sample_rate_hz input IQ rate
 * @param preamble_score_min preamble correlation threshold, in units of the block RMS
 * @param slice_magnitude_min bit-slice magnitude threshold, in units of the block RMS
 * @param snapshots whether to keep a DemodSnapshot of each block with frames
 * @param envelopes while it holds true, each block's own envelope outputs are copied into its DemodBlock; read
 *     once a block. Only while asked for, as at a low input rate they outnumber the block's samples.
 * @throws std::length_error (from the stream) if a block is too large
 * @throws std::invalid_argument (from the stream) if a block's padding is smaller than demod_padding()
 */
coro::CoroStream<DemodBlock> demodulate(coro::CoroStream<IqBlock> in, FilterBank filter, double sample_rate_hz,
                                        float preamble_score_min, float slice_magnitude_min, bool snapshots = false,
                                        std::optional<coro::WatchReceiver<bool>> envelopes = std::nullopt);
