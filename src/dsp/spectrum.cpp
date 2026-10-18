#include "dsp/spectrum.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include <fftw3.h>
#include <nlohmann/json.hpp>
#include <xtensor/xbuilder.hpp>
#include <xtensor/xjson.hpp>

struct SpectrumComputer::Impl {
    fftwf_complex* in;
    fftwf_complex* out;
    fftwf_plan plan;
    xt::xtensor<float, 1> window;

    Impl() {
        in = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * SpectrumComputer::kFftSize));
        out = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * SpectrumComputer::kFftSize));
        plan = fftwf_plan_dft_1d(SpectrumComputer::kFftSize, in, out, FFTW_FORWARD, FFTW_MEASURE);
        window = xt::empty<float>(std::array<size_t, 1>{SpectrumComputer::kFftSize});
        for (int i = 0; i < SpectrumComputer::kFftSize; i++) {
            window[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * i / (SpectrumComputer::kFftSize - 1)));
        }
    }
    ~Impl() {
        fftwf_destroy_plan(plan);
        fftwf_free(in);
        fftwf_free(out);
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

SpectrumComputer::SpectrumComputer() : m_impl(std::make_unique<Impl>()) {}
SpectrumComputer::~SpectrumComputer() = default;

xt::xtensor<float, 1> SpectrumComputer::compute(std::span<const float, 2 * kFftSize> iq) {
    auto& [in, out, plan, window] = *m_impl;
    for (int i = 0; i < SpectrumComputer::kFftSize; i++) {
        in[i][0] = iq[2 * i] * window[i];
        in[i][1] = iq[2 * i + 1] * window[i];
    }
    fftwf_execute(plan);
    xt::xtensor<float, 1> mag_db = xt::empty<float>(std::array<size_t, 1>{SpectrumComputer::kFftSize});
    for (int i = 0; i < SpectrumComputer::kFftSize; i++) {
        int src = (i + SpectrumComputer::kFftSize / 2) % SpectrumComputer::kFftSize;  // fftshift
        float re = out[src][0], im = out[src][1];
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
