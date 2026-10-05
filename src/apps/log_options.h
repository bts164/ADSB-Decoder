#pragma once

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>

#include <absl/log/globals.h>
#include <absl/log/initialize.h>
#include <absl/log/log_sink_registry.h>
#include <argparse/argparse.hpp>

#include "common/file_log_sink.h"
#include "common/log.h"

/**
 * @file
 * The logging command-line options the apps share.
 */

/** A --log-level / --log-file-level value, or nullopt if `name` isn't one. */
inline std::optional<absl::LogSeverityAtLeast> parse_log_level(const std::string& name) {
    if (name == "info") return absl::LogSeverityAtLeast::kInfo;
    if (name == "warning") return absl::LogSeverityAtLeast::kWarning;
    if (name == "error") return absl::LogSeverityAtLeast::kError;
    if (name == "fatal") return absl::LogSeverityAtLeast::kFatal;
    if (name == "none") return absl::LogSeverityAtLeast::kInfinity;
    return std::nullopt;
}

/**
 * Adds --log-level, --log-file, --log-file-level and --verbosity to `program`.
 * @param verbosity_levels what the program's verbosity levels add, for the --verbosity help
 */
inline void add_log_options(argparse::ArgumentParser& program,
                            const std::string& verbosity_levels = "higher for more") {
    // Rejects a bad level while parsing, so it's reported with the usage like any other bad argument.
    auto level = [](const std::string& option) {
        return [option](const std::string& name) {
            if (!parse_log_level(name))
                throw std::runtime_error(option + " must be info, warning, error, fatal or none, not '" + name + "'");
            return name;
        };
    };
    program.add_argument("--log-level")
        .default_value(std::string("info"))
        .action(level("--log-level"))
        .help("least severe log message to write to stderr: info, warning, error, fatal, or none to write "
              "nothing");
    program.add_argument("--log-file").help(
        "also append log messages to this file (default: stderr only)");
    program.add_argument("--log-file-level")
        .default_value(std::string("info"))
        .action(level("--log-file-level"))
        .help("least severe log message to write to --log-file, independent of --log-level");
    program.add_argument("--verbosity").scan<'i', int>().default_value(0).help(
        "verbosity of the extra diagnostic messages, which are logged at info: 0 for none, " + verbosity_levels);
}

/**
 * Starts logging to stderr and, with --log-file, to a file, as the options ask. Call once, after parsing the
 * arguments and before the first log statement.
 * @throws std::runtime_error if the log file can't be opened
 */
inline void init_logging(const argparse::ArgumentParser& program) {
    const absl::LogSeverityAtLeast stderr_level = *parse_log_level(program.get<std::string>("--log-level"));
    absl::LogSeverityAtLeast min_level = stderr_level;
    if (auto path = program.present("--log-file")) {
        const absl::LogSeverityAtLeast file_level = *parse_log_level(program.get<std::string>("--log-file-level"));
        // Never destroyed: a registered sink has to outlive every log statement, including ones in static
        // destructors.
        absl::AddLogSink(new FileLogSink(*path, file_level));
        min_level = std::min(min_level, file_level);
    }
    // The minimum level is what makes a disabled statement skip its formatting, so it's the lower of the two
    // destinations' levels: a message is formatted only if at least one of them wants it. Each destination
    // then applies its own level.
    absl::SetMinLogLevel(min_level);
    absl::SetStderrThreshold(stderr_level);
    absl::SetGlobalVLogLevel(program.get<int>("--verbosity"));
    absl::InitializeLog();
}
