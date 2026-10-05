#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include <absl/base/log_severity.h>
#include <absl/log/log_entry.h>
#include <absl/log/log_sink.h>

/**
 * @file
 * Writes log messages to a file.
 */

/**
 * An Abseil log sink that appends every message at or above a severity to a file, in the same format as
 * stderr gets. Register it with absl::AddLogSink(); it must then outlive all logging.
 *
 * Its severity is separate from stderr's. A message reaches the sink only if it is at or above Abseil's
 * minimum log level (absl::SetMinLogLevel), so that has to be at most this sink's level.
 */
class FileLogSink : public absl::LogSink {
public:
    /**
     * Opens `path` for appending, creating it if needed.
     * @param level least severe message to write
     * @throws std::runtime_error if the file can't be opened
     */
    FileLogSink(const std::string& path, absl::LogSeverityAtLeast level) : level_(level) {
        file_ = std::fopen(path.c_str(), "a");
        if (file_ == nullptr)
            throw std::runtime_error("cannot open log file " + path + ": " + std::strerror(errno));
        // Line buffered: each message is on disk when its statement returns, so the file is complete after
        // a crash and can be followed with `tail -f`.
        std::setvbuf(file_, nullptr, _IOLBF, 0);
    }
    ~FileLogSink() override { std::fclose(file_); }
    FileLogSink(const FileLogSink&) = delete;
    FileLogSink& operator=(const FileLogSink&) = delete;

    // Called from whichever thread logs, possibly several at once. One fwrite per message keeps them from
    // interleaving: stdio locks the stream for the length of each call.
    void Send(const absl::LogEntry& entry) override {
        if (entry.log_severity() < level_) return;
        const auto line = entry.text_message_with_prefix_and_newline();
        std::fwrite(line.data(), 1, line.size(), file_);
    }
    void Flush() override { std::fflush(file_); }

private:
    std::FILE* file_;
    absl::LogSeverityAtLeast level_;
};
