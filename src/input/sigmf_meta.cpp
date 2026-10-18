#include "input/sigmf_meta.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

std::string sigmf_meta_path(const std::string& iq_path) {
    const auto pos = iq_path.rfind(".sigmf-data");
    return pos != std::string::npos ? iq_path.substr(0, pos) + ".sigmf-meta" : iq_path + "-meta";
}

std::optional<SigmfMeta> read_sigmf_meta(const std::string& meta_path) {
    if (!std::filesystem::exists(meta_path)) return std::nullopt;
    std::ifstream in(meta_path);
    if (!in) throw std::runtime_error("can't read " + meta_path);
    try {
        const nlohmann::json j = nlohmann::json::parse(in);
        const nlohmann::json& global = j.at("global");
        SigmfMeta meta;
        if (global.contains("core:datatype")) meta.datatype = global["core:datatype"].get<std::string>();
        if (global.contains("core:sample_rate")) meta.sample_rate_hz = global["core:sample_rate"].get<double>();
        if (j.contains("captures") && j["captures"].is_array() && !j["captures"].empty() &&
            j["captures"][0].contains("core:frequency")) {
            meta.frequency_hz = j["captures"][0]["core:frequency"].get<double>();
        }
        return meta;
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(meta_path + ": " + e.what());
    }
}
