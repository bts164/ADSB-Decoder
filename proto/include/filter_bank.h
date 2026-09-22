#pragma once

#include <string>
#include <vector>

// Loaded polyphase filter bank coefficients, plane 0 only (the streaming
// demodulator doesn't use the derivative planes) — see README.md "Filter
// coefficient source". Layout matches Julia's column-major h[taps, phase]:
// coeffs[phase * taps + tap].
struct FilterBank {
    std::vector<float> coeffs;
    int taps = 0;
    int num_phases = 0;

    const float* phase(int p) const { return coeffs.data() + static_cast<size_t>(p) * taps; }
};

// Loads a raw-binary filter bank exported by proto/export_filter.jl:
// <bin_path> is a flat Float32 array (taps * Np * planes, Julia column-major),
// <meta_path> is a "key=value" text sidecar with taps/Np/planes/dtype.
FilterBank load_filter_bank(const std::string& bin_path, const std::string& meta_path);
