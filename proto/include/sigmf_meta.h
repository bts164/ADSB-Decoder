#pragma once

#include <optional>
#include <string>

// Minimal, deliberately non-general extraction of "core:sample_rate" from a
// .sigmf-meta JSON file -- avoids pulling in a JSON library just for this.
// Falls back to std::nullopt if not found; caller must supply --rate then.
std::optional<double> read_sigmf_sample_rate(const std::string& meta_path);
