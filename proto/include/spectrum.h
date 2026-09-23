#pragma once

#include <chrono>
#include <complex>
#include <vector>

#include <fftw3.h>
#include <nlohmann/json_fwd.hpp>

// --- Spectrum / waterfall -------------------------------------------------
//
// A side channel off the raw pre-resample IQ blocks handed to the compute
// thread (see pipeline.h's run_demod_loop) -- before AdsbDemod's own
// resampling to its fixed internal ADS-B rate, which would distort the
// frequency picture this is for (true center-frequency offset, out-of-band
// noise). Runs far below the full sample rate: one short windowed FFT out of
// one block every kSpectrumInterval, not a continuous transform over
// everything, since a waterfall display only needs a handful of updates per
// second.
constexpr int kSpectrumFftSize = 1024;
constexpr std::chrono::milliseconds kSpectrumInterval{150};

class SpectrumComputer {
public:
    SpectrumComputer();
    ~SpectrumComputer();

    SpectrumComputer(const SpectrumComputer&) = delete;
    SpectrumComputer& operator=(const SpectrumComputer&) = delete;

    // Windows and transforms exactly kSpectrumFftSize samples starting at
    // `x`, returning fftshifted magnitude in dB: bin 0 is the most negative
    // frequency (-rate_hz/2 relative to center) and the last bin the most
    // positive (+rate_hz/2), so callers can plot straight against a linear
    // frequency axis with no reordering.
    std::vector<float> compute(const std::complex<float>* x);

private:
    fftwf_complex* in_;
    fftwf_complex* out_;
    fftwf_plan plan_;
    std::vector<float> window_;
};

// Bundles one compute() result with the context needed to serialize it --
// spectrum_to_json used to take these as three loose parameters, but the
// nlohmann::json ADL to_json customization point (see frame_decode.h) needs
// a single object to hang off of.
struct SpectrumFrame {
    const std::vector<float>& mag_db;
    double rate_hz;
    double freq_hz;
};

void to_json(nlohmann::json& j, const SpectrumFrame& s);
