#pragma once

#include <absl/log/log.h>
#include <absl/strings/str_format.h>

/**
 * @file
 * printf-style logging on top of Abseil's log library.
 *
 *     LOGF(INFO, "serving %s on port %u", path, port);
 *     LOGF(ERROR, "filter bank: %s", e.what());
 *     VLOGF(1, "%zu samples, %.2f Msamples/s", n, rate / 1e6);
 *
 * The format string is checked against its arguments at compile time (absl::StrFormat rules: %s takes
 * std::string and std::string_view as well as const char*, and %d any integer type).
 *
 * A disabled statement costs one comparison. LOG and VLOG evaluate what is streamed into them only when the
 * message will be logged, and absl::StreamFormat does its formatting when streamed, so neither the
 * formatting nor the argument expressions run for a message below --log-level or above --verbosity.
 *
 * The severities are Abseil's: INFO, WARNING, ERROR, FATAL (which aborts). Abseil's other LOG macros can be
 * used directly; the same wrapping works for any of them.
 */

/** Logs a printf-style message at `severity` (INFO, WARNING, ERROR or FATAL). */
#define LOGF(severity, ...) LOG(severity) << absl::StreamFormat(__VA_ARGS__)

/** Logs a printf-style message at INFO if the verbosity (--verbosity) is at least `level`. */
#define VLOGF(level, ...) VLOG(level) << absl::StreamFormat(__VA_ARGS__)

/** LOGF, at most once every `seconds` from this statement. */
#define LOGF_EVERY_N_SEC(severity, seconds, ...) LOG_EVERY_N_SEC(severity, seconds) << absl::StreamFormat(__VA_ARGS__)
