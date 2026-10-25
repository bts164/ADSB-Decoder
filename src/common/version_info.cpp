#include "common/version_info.h"

#include <link.h>

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace adsb::version_info {

namespace {

std::string compiler() {
#if defined(__clang__)
    return "clang " __clang_version__;
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#else
    return "unknown compiler";
#endif
}

// Every shared object in the process, in load order. The program itself has no name and is left out.
std::vector<std::string> loaded_shared_objects() {
    std::vector<std::string> paths;
    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t /*size*/, void* data) {
            if (info->dlpi_name != nullptr && info->dlpi_name[0] != '\0')
                static_cast<std::vector<std::string>*>(data)->push_back(info->dlpi_name);
            return 0;
        },
        &paths);
    return paths;
}

}  // namespace

std::string summary_line() {
    const nlohmann::json conan = nlohmann::json::parse(generated::conan_json());
    std::string line = std::string(git_describe()) + ", " + conan["settings"].value("build_type", "unknown") +
                       " build, " + compiler();
    for (const nlohmann::json& dep : conan["dependencies"]) {
        const std::string name = dep["name"];
        if (name.starts_with("coro/")) line += ", " + name;
    }
    return line;
}

std::string report(std::string_view program, Level level, const std::vector<RuntimeLibrary>& runtime) {
    if (level == Level::json) {
        nlohmann::json j;
        j["program"] = program;
        j["git_describe"] = git_describe();
        j["compiler"] = compiler();
        j["conan"] = nlohmann::json::parse(generated::conan_json());
        j["runtime_libraries"] = nlohmann::json::object();
        for (const RuntimeLibrary& library : runtime) j["runtime_libraries"][library.name] = library.version;
        j["loaded_shared_objects"] = loaded_shared_objects();
        return j.dump(2);
    }
    std::string text = std::string(program) + " " + std::string(git_describe()) + ", built with " + compiler() +
                       "\n";
    switch (level) {
    case Level::summary: text += generated::conan_summary(); break;
    case Level::deps: text += generated::conan_deps(); break;
    default: text += generated::conan_full(); break;
    }
    text += "\n";
    if (!runtime.empty()) {
        text += "runtime libraries:\n";
        for (const RuntimeLibrary& library : runtime) text += "  " + library.name + " " + library.version + "\n";
    }
    if (level == Level::full) {
        text += "loaded shared objects:\n";
        for (const std::string& path : loaded_shared_objects()) text += "  " + path + "\n";
    }
    text.pop_back();  // the caller ends the line
    return text;
}

}  // namespace adsb::version_info
