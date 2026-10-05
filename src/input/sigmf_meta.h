#pragma once

#include <optional>
#include <string>

/**
 * Reads `core:sample_rate` from a .sigmf-meta file. A minimal text scan, not a general JSON parse.
 * @return the sample rate in Hz, or std::nullopt if the file can't be read or has no sample rate
 */
std::optional<double> read_sigmf_sample_rate(const std::string& meta_path);
