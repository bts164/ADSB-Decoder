#include "sigmf_meta.h"

#include <fstream>

std::optional<double> read_sigmf_sample_rate(const std::string& meta_path) {
    std::ifstream in(meta_path);
    if (!in) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string key = "\"core:sample_rate\"";
    auto pos = content.find(key);
    if (pos == std::string::npos) return std::nullopt;
    pos = content.find(':', pos + key.size());
    if (pos == std::string::npos) return std::nullopt;
    return std::stod(content.substr(pos + 1));
}
