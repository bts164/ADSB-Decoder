#pragma once

#include <chrono>
#include <complex>
#include <memory>
#include <vector>

#include <nlohmann/json_fwd.hpp>

/**
 * @file
 * Spectrum for the UI waterfall, computed from the raw pre-resample IQ.
 *
 * A side channel off the blocks run_demod_loop (pipeline.h) hands to AdsbDemod: before resampling, so it
 * shows the true center-frequency offset and out-of-band noise. One windowed FFT of one block every
 * kSpectrumInterval, not a continuous transform, since a waterfall only needs a few updates per second.
 */

constexpr int kSpectrumFftSize = 1024;  /**< FFT length, in samples */
constexpr std::chrono::milliseconds kSpectrumInterval{150};  /**< minimum time between spectrum frames */

/** Hann-windowed FFT of kSpectrumFftSize samples, with its FFTW plan and buffers. */
class SpectrumComputer {
public:
    SpectrumComputer();
    ~SpectrumComputer();

    /**
     * Windows and transforms kSpectrumFftSize samples.
     * @param x first of kSpectrumFftSize samples
     * @return fftshifted magnitude in dB: bin 0 is the most negative frequency (-rate/2 relative to
     *         center) and the last bin the most positive, ready to plot against a linear frequency axis
     */
    std::vector<float> compute(const std::complex<float>* x);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** One SpectrumComputer::compute() result with the context needed to serialize it. */
struct SpectrumFrame {
    const std::vector<float>& mag_db;
    double rate_hz;  /**< input sample rate, i.e. the span of the bins */
    double freq_hz;  /**< center frequency */
};

/** nlohmann::json conversion (found by ADL): `{"type":"spectrum", ...}`. */
void to_json(nlohmann::json& j, const SpectrumFrame& s);
