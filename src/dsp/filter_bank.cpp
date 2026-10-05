#include "dsp/filter_bank.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <xtensor/xbuilder.hpp>
#include <xtensor/xmanipulation.hpp>
#include <xtensor/xview.hpp>

#include "common/constants.h"

namespace {

// >= the widest ispc gang (16 lanes) so the kernel's tap loop never needs a partial final gang.
constexpr int kKernelStrideAlign = 16;

std::map<std::string, std::string> parse_kv_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open filter meta file: " + path);
    std::map<std::string, std::string> kv;
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

// Fills in coeffs and kernel_coeffs from `plane` ({num_phases, taps}, in polyphase.jl's tap order).
FilterBank finish(FilterBank fb, const xt::xtensor<float, 2>& plane) {
    // Reverse tap order within each phase: polyphase.jl's own resample()
    // reference applies taps as h[Mh-m+1, pi, k] against x[i+m] -- reversed
    // relative to the window offset m -- but kernels.ispc's MAC loop walks
    // both h[] and x[] forward with the same increasing k (see
    // resample_block). Reversing once here makes the two match without
    // touching the kernel's hot loop or its contiguous access pattern.
    fb.coeffs = xt::flip(plane, 1);

    const size_t np = fb.num_phases, taps = fb.taps;
    fb.kernel_stride = (2 * fb.taps + kKernelStrideAlign - 1) / kKernelStrideAlign * kKernelStrideAlign;
    fb.kernel_coeffs = xt::zeros<float>({np, static_cast<size_t>(fb.kernel_stride)});
    xt::view(fb.kernel_coeffs, xt::all(), xt::range(size_t{0}, 2 * taps, size_t{2})) = fb.coeffs;
    xt::view(fb.kernel_coeffs, xt::all(), xt::range(size_t{1}, 2 * taps, size_t{2})) = fb.coeffs;
    return fb;
}

}  // namespace

FilterWindow parse_filter_window(const std::string& name) {
    if (name == "hann") return FilterWindow::hann;
    if (name == "hamming") return FilterWindow::hamming;
    if (name == "blackman") return FilterWindow::blackman;
    if (name == "kaiser") return FilterWindow::kaiser;
    throw std::invalid_argument("unknown filter window '" + name + "' (hann, hamming, blackman or kaiser)");
}

namespace {
// Symmetric window of length n at point k.
double window_at(const FilterDesign& d, int k, int n) {
    if (n == 1) return 1.0;
    const double x = static_cast<double>(k) / (n - 1);
    switch (d.window) {
        case FilterWindow::hann: return 0.5 * (1 - std::cos(2 * kPi * x));
        case FilterWindow::hamming: return 0.54 - 0.46 * std::cos(2 * kPi * x);
        case FilterWindow::blackman: return 0.42 - 0.5 * std::cos(2 * kPi * x) + 0.08 * std::cos(4 * kPi * x);
        case FilterWindow::kaiser: {
            const double r = 2 * x - 1;
            return std::cyl_bessel_i(0.0, d.kaiser_beta * std::sqrt(std::max(0.0, 1 - r * r))) /
                   std::cyl_bessel_i(0.0, d.kaiser_beta);
        }
    }
    return 1.0;
}

const char* window_name(FilterWindow w) {
    switch (w) {
        case FilterWindow::hann: return "hann";
        case FilterWindow::hamming: return "hamming";
        case FilterWindow::blackman: return "blackman";
        case FilterWindow::kaiser: return "kaiser";
    }
    return "?";
}
}  // namespace

FilterBank design_filter_bank(const FilterDesign& d, double input_rate_hz, double output_rate_hz) {
    const int taps = d.taps, num_phases = d.num_phases;
    if (taps < 1) throw std::invalid_argument("filter taps must be positive");
    if (num_phases < 1) throw std::invalid_argument("filter phases must be positive");
    if (!(input_rate_hz > 0) || !(output_rate_hz > 0) || !(d.cutoff_hz > 0))
        throw std::invalid_argument("filter sample rates and cutoff must be positive");
    if (d.pulse_width_s < 0 || d.kaiser_beta < 0)
        throw std::invalid_argument("filter pulse width and Kaiser beta must not be negative");

    FilterBank fb;
    fb.taps = taps;
    fb.num_phases = num_phases;
    fb.cutoff_hz = std::min({d.cutoff_hz, input_rate_hz / 2, output_rate_hz / 2});

    // Pulse boxcar length, in prototype samples (num_phases * input rate). The lowpass is pulse - 1 shorter
    // than n, so their convolution is exactly n long and has the same center.
    const int n = taps * num_phases;
    const int pulse = d.pulse_width_s > 0
                          ? std::max(1, static_cast<int>(std::lround(d.pulse_width_s * input_rate_hz * num_phases)))
                          : 1;
    if (pulse >= n) throw std::invalid_argument("filter pulse width is longer than the filter");
    const int n_lp = n - pulse + 1;

    // Prototype lowpass at num_phases * input rate, as DSP.jl's
    // digitalfilter(Lowpass(w), FIRWindow(window(n))) builds it: w is the
    // cutoff as a fraction of that rate's Nyquist, h[k] = w * sinc(w * (k -
    // (n-1)/2)) times the window, then scaled to unity DC gain.
    const double w = 2 * fb.cutoff_hz / (input_rate_hz * num_phases);
    xt::xtensor<double, 1> lp = xt::empty<double>(std::array<size_t, 1>{static_cast<size_t>(n_lp)});
    for (int k = 0; k < n_lp; k++) {
        const double t = w * (k - (n_lp - 1) / 2.0);
        const double sinc = t == 0 ? 1.0 : std::sin(kPi * t) / (kPi * t);
        lp(static_cast<size_t>(k)) = w * sinc * window_at(d, k, n_lp);
    }
    xt::xtensor<double, 1> h = xt::zeros<double>({static_cast<size_t>(n)});
    // The lowpass convolved with the pulse boxcar: `pulse` shifted copies of it, summed. Descending shifts
    // add each coefficient's terms in ascending lowpass order.
    for (int j = pulse - 1; j >= 0; j--) xt::view(h, xt::range(j, j + n_lp)) += lp;
    const double sum = std::accumulate(h.begin(), h.end(), 0.0);

    char desc[160];
    int len = std::snprintf(desc, sizeof desc, "%s", window_name(d.window));
    if (d.window == FilterWindow::kaiser) len += std::snprintf(desc + len, sizeof desc - len, " beta %g", d.kaiser_beta);
    len += std::snprintf(desc + len, sizeof desc - len, ", cutoff %g MHz", fb.cutoff_hz / 1e6);
    if (pulse > 1) std::snprintf(desc + len, sizeof desc - len, ", matched to %g us pulses", d.pulse_width_s * 1e6);
    fb.description = desc;

    // Phase p is every num_phases-th coefficient from p; each phase gets the
    // num_phases gain back, so every phase has roughly unity DC gain.
    auto plane = xt::xtensor<float, 2>::from_shape({static_cast<size_t>(num_phases), static_cast<size_t>(taps)});
    for (int p = 0; p < num_phases; p++)
        for (int m = 0; m < taps; m++)
            plane(p, m) = static_cast<float>(num_phases * h(static_cast<size_t>(m * num_phases + p)) / sum);
    return finish(std::move(fb), plane);
}

FilterBank load_filter_bank(const std::string& bin_path, const std::string& meta_path) {
    auto kv = parse_kv_file(meta_path);
    if (kv.count("dtype") && kv["dtype"] != "float32") {
        throw std::runtime_error("unsupported filter dtype: " + kv["dtype"] + " (only float32 supported)");
    }

    FilterBank fb;
    fb.description = "loaded " + bin_path;
    fb.taps = std::stoi(kv.at("taps"));
    fb.num_phases = std::stoi(kv.at("Np"));
    int planes = kv.count("planes") ? std::stoi(kv["planes"]) : 1;

    std::ifstream in(bin_path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open filter bin file: " + bin_path);

    // Plane 0 only: the first taps*num_phases floats of the column-major
    // (taps, Np, planes) array, i.e. a row-major {Np, taps} array.
    auto plane0 = xt::xtensor<float, 2>::from_shape({static_cast<size_t>(fb.num_phases), static_cast<size_t>(fb.taps)});
    in.read(reinterpret_cast<char*>(plane0.data()), static_cast<std::streamsize>(plane0.size() * sizeof(float)));
    if (!in) throw std::runtime_error("filter bin file shorter than taps*Np*4 bytes: " + bin_path);
    (void)planes;  // remaining planes (derivatives), if any, are ignored

    return finish(std::move(fb), plane0);
}
