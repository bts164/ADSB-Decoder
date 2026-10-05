#include "dsp/spectrum.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <fftw3.h>
#include <nlohmann/json.hpp>
#include <xtensor/xbuilder.hpp>
#include <xtensor/xjson.hpp>

#include "common/constants.h"

struct SpectrumComputer::Impl {
    fftwf_complex* in;
    fftwf_complex* out;
    fftwf_plan plan;
    xt::xtensor<float, 1> window;

    Impl() {
        in = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * kSpectrumFftSize));
        out = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * kSpectrumFftSize));
        plan = fftwf_plan_dft_1d(kSpectrumFftSize, in, out, FFTW_FORWARD, FFTW_MEASURE);
        window = xt::empty<float>(std::array<size_t, 1>{kSpectrumFftSize});
        for (int i = 0; i < kSpectrumFftSize; i++) {
            window[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(kPi) * i / (kSpectrumFftSize - 1)));
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

SpectrumComputer::SpectrumComputer() : impl_(std::make_unique<Impl>()) {}
SpectrumComputer::~SpectrumComputer() = default;

xt::xtensor<float, 1> SpectrumComputer::compute(std::span<const float, 2 * kSpectrumFftSize> iq) {
    auto& [in, out, plan, window] = *impl_;
    for (int i = 0; i < kSpectrumFftSize; i++) {
        in[i][0] = iq[2 * i] * window[i];
        in[i][1] = iq[2 * i + 1] * window[i];
    }
    fftwf_execute(plan);
    xt::xtensor<float, 1> mag_db = xt::empty<float>(std::array<size_t, 1>{kSpectrumFftSize});
    for (int i = 0; i < kSpectrumFftSize; i++) {
        int src = (i + kSpectrumFftSize / 2) % kSpectrumFftSize;  // fftshift
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
