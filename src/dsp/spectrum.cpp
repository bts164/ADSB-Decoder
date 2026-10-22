#include "dsp/spectrum.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <span>
#include <type_traits>
#include <utility>

#include <coro/coro.h>
#include <fftw3.h>
#include <nlohmann/json.hpp>
#include <xtensor/xbuilder.hpp>
#include <xtensor/xjson.hpp>

#include "common/log.h"
#include "common/trace.h"

namespace {

constexpr size_t kFftSize = 4096;  // FFT length, in samples

struct FftwFree {
    void operator()(fftwf_complex* p) const { fftwf_free(p); }
};
using FftwBuffer = std::unique_ptr<fftwf_complex[], FftwFree>;

struct FftwDestroyPlan {
    void operator()(fftwf_plan p) const { fftwf_destroy_plan(p); }
};
using FftwPlan = std::unique_ptr<std::remove_pointer_t<fftwf_plan>, FftwDestroyPlan>;

FftwBuffer fftw_buffer() {
    return FftwBuffer(static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * kFftSize)));
}

}  // namespace

coro::CoroStream<SpectrumFrame> spectrum_rows(coro::CoroStream<IqBlock> in, std::chrono::milliseconds interval,
                                              double rate_hz, double freq_hz) {
    LOGF(INFO, "spectrum: a row of %zu bins at most every %lld ms, peak-held over the FFTs since the last",
         kFftSize, static_cast<long long>(interval.count()));
    FftwBuffer fft_in = fftw_buffer();
    FftwBuffer fft_out = fftw_buffer();
    const FftwPlan plan(fftwf_plan_dft_1d(kFftSize, fft_in.get(), fft_out.get(), FFTW_FORWARD, FFTW_MEASURE));
    // fftwf_complex is float[2], so the input buffer is kFftSize interleaved I/Q pairs.
    const std::span<float> iq(reinterpret_cast<float*>(fft_in.get()), 2 * kFftSize);
    std::array<float, kFftSize> window;
    for (size_t i = 0; i < kFftSize; i++)
        window[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * i / (kFftSize - 1)));
    std::array<float, kFftSize> peak_mag2{};  // fftshifted
    bool pending = false;

    auto next_at = std::chrono::steady_clock::now() + interval;
    while (auto block = co_await coro::next(in)) {
        {
            TRACE_ZONE("spectrum_compute");
            const size_t lead = static_cast<size_t>(block->lead());
            for (size_t first = 0; first + kFftSize <= block->n(); first += kFftSize) {
                block->to_float(lead + first, iq);
                for (size_t i = 0; i < kFftSize; i++) {
                    fft_in[i][0] *= window[i];
                    fft_in[i][1] *= window[i];
                }
                fftwf_execute(plan.get());
                for (size_t i = 0; i < kFftSize; i++) {
                    const size_t src = (i + kFftSize / 2) % kFftSize;  // fftshift
                    const float re = fft_out[src][0], im = fft_out[src][1];
                    peak_mag2[i] = std::max(peak_mag2[i], re * re + im * im);
                }
                pending = true;
            }
        }
        if (const auto now = std::chrono::steady_clock::now(); now >= next_at && pending) {
            next_at = now + interval;
            xt::xtensor<float, 1> mag_db = xt::empty<float>(std::array<size_t, 1>{kFftSize});
            for (size_t i = 0; i < kFftSize; i++) {
                mag_db[i] = 10.0f * std::log10(std::max(peak_mag2[i], 1e-20f));
                peak_mag2[i] = 0;
            }
            pending = false;
            co_yield SpectrumFrame{std::move(mag_db), rate_hz, freq_hz};
        }
    }
}

void to_json(nlohmann::json& j, const SpectrumFrame& s) {
    j["type"] = "spectrum";
    j["freq_hz"] = s.freq_hz;
    j["rate_hz"] = s.rate_hz;
    j["bins"] = s.mag_db;
}
