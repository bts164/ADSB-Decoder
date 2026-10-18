#pragma once

#include <optional>
#include <string>

/** What `adsb file` reads from a .sigmf-meta file. A field the file doesn't give is std::nullopt. */
struct SigmfMeta {
    std::optional<std::string> datatype;   ///< global core:datatype, e.g. "cf32_le"
    std::optional<double> sample_rate_hz;  ///< global core:sample_rate
    std::optional<double> frequency_hz;    ///< the first capture's core:frequency
};

/**
 * The .sigmf-meta file that goes with a recording: `iq_path` with .sigmf-data replaced by .sigmf-meta, or with
 * "-meta" appended if it doesn't end in .sigmf-data.
 */
std::string sigmf_meta_path(const std::string& iq_path);

/**
 * Reads a .sigmf-meta file.
 * @return std::nullopt if the file doesn't exist
 * @throws std::runtime_error if it exists but can't be read, isn't JSON, or a field has the wrong type
 */
std::optional<SigmfMeta> read_sigmf_meta(const std::string& meta_path);
