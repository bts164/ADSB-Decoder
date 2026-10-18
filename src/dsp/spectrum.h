#pragma once

#include <memory>
#include <span>

#include <nlohmann/json_fwd.hpp>
#include <xtensor/xtensor.hpp>

/**
 * @file
 * Spectrum for the UI waterfall, computed from the raw pre-resample IQ.
 *
 * A side channel off the blocks run_demod_loop (pipeline.h) hands to demodulate(): before resampling, so it
 * shows the true center-frequency offset and out-of-band noise. One windowed FFT of one block every so often,
 * not a continuous transform, since a waterfall only needs a few updates per second.
 */

/** Hann-windowed FFT of kFftSize samples, with its FFTW plan and buffers. */
class SpectrumComputer {
public:
    static constexpr int kFftSize = 1024;  /**< FFT length, in samples */

    SpectrumComputer();
    ~SpectrumComputer();

    /**
     * Windows and transforms kFftSize samples.
     * @param iq kFftSize samples, as interleaved I/Q
     * @return fftshifted magnitude in dB: bin 0 is the most negative frequency (-rate/2 relative to
     *         center) and the last bin the most positive, ready to plot against a linear frequency axis
     */
    xt::xtensor<float, 1> compute(std::span<const float, 2 * kFftSize> iq);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/** One SpectrumComputer::compute() result with the context needed to serialize it. */
struct SpectrumFrame {
    const xt::xtensor<float, 1>& mag_db;
    double rate_hz;  /**< input sample rate, i.e. the span of the bins */
    double freq_hz;  /**< center frequency */
};

/** nlohmann::json conversion (found by ADL): `{"type":"spectrum", ...}`. */
void to_json(nlohmann::json& j, const SpectrumFrame& s);
