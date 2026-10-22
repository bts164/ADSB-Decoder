#pragma once

#include <chrono>

#include <coro/coro_stream.h>
#include <nlohmann/json_fwd.hpp>
#include <xtensor/xtensor.hpp>

#include "dsp/iq_block.h"

/**
 * @file
 * Spectrum for the UI waterfall, computed from the raw pre-resample IQ.
 *
 * A side channel off the blocks run_demod_loop (pipeline.h) hands to demodulate(): before resampling, so it
 * shows the true center-frequency offset and out-of-band noise. Each row is a peak-hold over the FFTs since the
 * last one, so a burst as short as an ADS-B reply shows even though the row covers far longer.
 */

/** One spectrum row with the context needed to serialize it. */
struct SpectrumFrame {
    /** Peak magnitude in dB, fftshifted: bin 0 is the most negative frequency (-rate/2 relative to center) and
     *  the last bin the most positive, ready to plot against a linear frequency axis. */
    xt::xtensor<float, 1> mag_db;
    double rate_hz;  /**< input sample rate, i.e. the span of the bins */
    double freq_hz;  /**< center frequency */
};

/**
 * Hann-windowed FFTs of each block from `in`, peak-held per bin, yielded as a row at most every `interval`
 * and only once a block has arrived since the last, so peaks held when blocks stop arriving go out in the
 * first row after they resume. Ends when `in` closes. Logs the row size and interval at INFO when first polled.
 * @param in blocks to transform, all of each block but a last part shorter than one FFT
 * @param interval least time between rows
 * @param rate_hz input sample rate, for labelling the rows
 * @param freq_hz center frequency, for labelling the rows
 */
coro::CoroStream<SpectrumFrame> spectrum_rows(coro::CoroStream<IqBlock> in, std::chrono::milliseconds interval,
                                              double rate_hz, double freq_hz);

/** nlohmann::json conversion (found by ADL): `{"type":"spectrum", ...}`. */
void to_json(nlohmann::json& j, const SpectrumFrame& s);
