#include "filter_bank.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {

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

}  // namespace

FilterBank load_filter_bank(const std::string& bin_path, const std::string& meta_path) {
    auto kv = parse_kv_file(meta_path);
    if (kv.count("dtype") && kv["dtype"] != "float32") {
        throw std::runtime_error("unsupported filter dtype: " + kv["dtype"] + " (only float32 supported)");
    }

    FilterBank fb;
    fb.taps = std::stoi(kv.at("taps"));
    fb.num_phases = std::stoi(kv.at("Np"));
    int planes = kv.count("planes") ? std::stoi(kv["planes"]) : 1;

    std::ifstream in(bin_path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open filter bin file: " + bin_path);

    // Plane 0 only: the first taps*num_phases floats of the column-major
    // (taps, Np, planes) array.
    size_t plane0_count = static_cast<size_t>(fb.taps) * fb.num_phases;
    fb.coeffs.resize(plane0_count);
    in.read(reinterpret_cast<char*>(fb.coeffs.data()), plane0_count * sizeof(float));
    if (!in) throw std::runtime_error("filter bin file shorter than taps*Np*4 bytes: " + bin_path);
    (void)planes;  // remaining planes (derivatives), if any, are ignored

    // Reverse tap order within each phase: polyphase.jl's own resample()
    // reference applies taps as h[Mh-m+1, pi, k] against x[i+m] -- reversed
    // relative to the window offset m -- but kernels.ispc's MAC loop walks
    // both h[] and x[] forward with the same increasing k (see
    // resample_block). Reversing once here, at load time, makes the two
    // match without touching the kernel's hot loop or its (now-contiguous)
    // access pattern.
    for (int p = 0; p < fb.num_phases; p++) {
        float* col = fb.coeffs.data() + static_cast<size_t>(p) * fb.taps;
        std::reverse(col, col + fb.taps);
    }

    return fb;
}
