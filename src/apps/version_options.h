#pragma once

#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <argparse/argparse.hpp>

#include "common/version_info.h"

/**
 * @file
 * The --version option the apps share. An app constructs its parser with `argparse::default_arguments::help`,
 * so argparse adds no -v/--version of its own.
 */

/** --version [summary|deps|full|json]: what went into this build (common/version_info.h). */
struct VersionOptions {
    std::optional<adsb::version_info::Level> level;  ///< set if --version was given

    /** Adds --version to `program`. */
    static void add_options(argparse::ArgumentParser& program) {
        program.add_argument("--version")
            .choices("summary", "deps", "full", "json")
            // Both, in this order: implicit_value() alone would take no value at all.
            .implicit_value(std::string("summary"))
            .nargs(argparse::nargs_pattern::optional)
            .help("print what went into this build and exit: summary (with no value) is the version, compiler, "
                  "options and direct dependencies; deps adds the Conan profiles, every dependency and build "
                  "tool and their options; full adds each one's recipe revision, package ID and package revision "
                  "and every shared object loaded; json is all of it as JSON");
    }

    static VersionOptions from_options(const argparse::ArgumentParser& program) {
        if (!program.is_used("--version")) return {};
        const std::string level = program.get<std::string>("--version");
        if (level == "deps") return {adsb::version_info::Level::deps};
        if (level == "full") return {adsb::version_info::Level::full};
        if (level == "json") return {adsb::version_info::Level::json};
        return {adsb::version_info::Level::summary};
    }

    /**
     * If --version was given, prints the report to stdout and exits, as --help does. Call it after parsing, and
     * also when parsing threw, so --version works without the options an app otherwise requires.
     * @param runtime_libraries asked only when --version was given, as asking can mean loading a library
     */
    static void exit_if_given(
        const argparse::ArgumentParser& program, std::string_view name,
        const std::function<std::vector<adsb::version_info::RuntimeLibrary>()>& runtime_libraries = {}) {
        const std::optional<adsb::version_info::Level> level = from_options(program).level;
        if (!level) return;
        std::vector<adsb::version_info::RuntimeLibrary> runtime;
        if (runtime_libraries) runtime = runtime_libraries();
        std::cout << adsb::version_info::report(name, *level, runtime) << "\n";
        std::exit(EXIT_SUCCESS);
    }
};
