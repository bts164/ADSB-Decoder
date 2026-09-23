#include "spectrum.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

#include "constants.h"

SpectrumComputer::SpectrumComputer() {
    in_ = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * kSpectrumFftSize));
    out_ = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * kSpectrumFftSize));
    plan_ = fftwf_plan_dft_1d(kSpectrumFftSize, in_, out_, FFTW_FORWARD, FFTW_MEASURE);
    window_.resize(kSpectrumFftSize);
    for (int i = 0; i < kSpectrumFftSize; i++) {
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(kPi) * i / (kSpectrumFftSize - 1)));
    }
}

SpectrumComputer::~SpectrumComputer() {
    fftwf_destroy_plan(plan_);
    fftwf_free(in_);
    fftwf_free(out_);
}

std::vector<float> SpectrumComputer::compute(const std::complex<float>* x) {
    for (int i = 0; i < kSpectrumFftSize; i++) {
        in_[i][0] = x[i].real() * window_[i];
        in_[i][1] = x[i].imag() * window_[i];
    }
    fftwf_execute(plan_);
    std::vector<float> mag_db(kSpectrumFftSize);
    for (int i = 0; i < kSpectrumFftSize; i++) {
        int src = (i + kSpectrumFftSize / 2) % kSpectrumFftSize;  // fftshift
        float re = out_[src][0], im = out_[src][1];
        float mag2 = re * re + im * im;
        mag_db[i] = 10.0f * std::log10(std::max(mag2, 1e-20f));
    }
    return mag_db;
}

void to_json(nlohmann::json& j, const SpectrumFrame& s) {
    j["type"] = "spectrum";
    j["freq_hz"] = s.freq_hz;
    j["rate_hz"] = s.rate_hz;
    j["bins"] = s.mag_db;
}
