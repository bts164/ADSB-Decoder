#include "output/capture.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <unistd.h>

#include <coro/coro_stream.h>
#include <coro/io/file.h>
#include <coro/sync/select.h>
#include <coro/sync/when.h>
#include <nlohmann/json.hpp>

#include "common/log.h"
#include "common/trace.h"

namespace {

// Most files list_capture_files() lists, so a directory of many recordings still fits in one message.
constexpr size_t kMaxListedFiles = 1000;

// Time between a capture's progress reports.
constexpr auto kReportInterval = std::chrono::milliseconds(500);

static_assert(std::endian::native == std::endian::little, "cf32_le and rf32_le are written as the host's floats");

CaptureCall::Report failure(wsrpc::Code code, const std::string& message) {
    return std::unexpected(wsrpc::CallError(code, message));
}

// What a capture task needs of each item it takes: its first sample's stream index at the point, its sample
// count, its SigMF datatype, and a run of its samples as bytes in that datatype. A copy, which only a capture
// in progress makes, so the item's buffer goes back to its source at once either way.

uint64_t first_index(const IqBlock& b) { return b.start(); }
size_t count(const IqBlock& b) { return b.n(); }
std::string datatype(const IqBlock& b) {
    return b.visit([]<class F>(IqView<F>) {
        if constexpr (std::is_same_v<F, iq::F32>) return "cf32_le";
        else return "ci16_be";
    });
}
std::vector<std::byte> bytes(const IqBlock& b, size_t from, size_t n) {
    return b.visit([&]<class F>(IqView<F> v) {
        const auto run = std::as_bytes(v.iq().subspan(2 * from, 2 * n));
        return std::vector<std::byte>(run.begin(), run.end());
    });
}

uint64_t first_index(const EnvelopeRun& r) { return r.index; }
size_t count(const EnvelopeRun& r) { return r.samples.size(); }
std::string datatype(const EnvelopeRun&) { return "rf32_le"; }
std::vector<std::byte> bytes(const EnvelopeRun& r, size_t from, size_t n) {
    const auto run = std::as_bytes(std::span(r.samples).subspan(from, n));
    return std::vector<std::byte>(run.begin(), run.end());
}

// The capture in progress.
struct Capture {
    coro::MpscSender<CaptureCall::Report> progress;
    std::string name;
    std::string data_path;
    std::string meta_path;
    coro::File data;
    uint64_t samples;                  // to cover, in stream time
    std::optional<uint64_t> start;     // stream index of the first sample, once the first item has arrived
    uint64_t next = 0;                 // stream index of the next sample to write
    uint64_t written = 0;
    std::string datatype;
    nlohmann::json segments = nlohmann::json::array();  // SigMF captures, one per run without a gap
    std::chrono::steady_clock::time_point next_report = std::chrono::steady_clock::now() + kReportInterval;
};

// Bytes per sample of a SigMF core:datatype, such as cf32_le (8) or ru8 (1); 0 for one it can't parse.
size_t sample_bytes(const std::string& datatype) {
    if (datatype.size() < 3 || (datatype[0] != 'c' && datatype[0] != 'r')) return 0;
    if (datatype[1] != 'f' && datatype[1] != 'i' && datatype[1] != 'u') return 0;
    size_t bits = 0;
    for (size_t i = 2; i < datatype.size() && std::isdigit(static_cast<unsigned char>(datatype[i])); i++)
        bits = bits * 10 + static_cast<size_t>(datatype[i] - '0');
    if (bits == 0 || bits % 8 != 0) return 0;
    return bits / 8 * (datatype[0] == 'c' ? 2 : 1);
}

// The duration of a recording's samples, from its size, datatype and rate; 0 for a datatype it can't parse.
double duration_s(uint64_t data_bytes, const api::CaptureMeta& meta) {
    const size_t bytes = sample_bytes(meta.datatype());
    if (bytes == 0 || !(meta.rate_hz() > 0)) return 0;
    return static_cast<double>(data_bytes / bytes) / meta.rate_hz();
}

// Fills in what `meta` says of `f`'s recording. A missing or invalid .sigmf-meta leaves f.meta unset.
void read_meta(api::Capture& f, const std::filesystem::path& meta) {
    std::ifstream in(meta);
    if (!in) return;
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    api::CaptureMeta m;
    try {
        const nlohmann::json& global = j.at("global");
        m.set_datatype(global.at("core:datatype").get<std::string>());
        m.set_rate_hz(global.at("core:sample_rate").get<double>());
        m.set_description(global.value("core:description", ""));
        if (const auto captures = j.find("captures");
            captures != j.end() && captures->is_array() && !captures->empty() &&
            captures->front().contains("core:frequency"))
            m.set_freq_hz(captures->front().at("core:frequency").get<double>());
    } catch (const nlohmann::json::exception&) {
        return;
    }
    m.set_duration_s(duration_s(f.data_bytes(), m));
    *f.mutable_meta() = std::move(m);
}

std::string description(const CapturePoint& point) { return std::string("adsb capture of the ") + point.name; }

// The stream time a capture has covered that isn't in its file.
uint64_t missing(const Capture& c) { return c.start ? c.next - *c.start - c.written : 0; }

coro::Coro<void> write_meta(const Capture& c, const CapturePoint& point) {
    nlohmann::json global = {
        {"core:datatype", c.datatype},
        {"core:sample_rate", point.rate_hz},
        {"core:version", "1.0.0"},
        {"core:recorder", "adsb"},
        {"core:description", description(point)},
    };
    const nlohmann::json meta = {{"global", std::move(global)}, {"captures", c.segments},
                                 {"annotations", nlohmann::json::array()}};
    coro::File file = co_await coro::File::open(c.meta_path, coro::FileMode::Write | coro::FileMode::Create |
                                                                 coro::FileMode::Truncate);
    (void)co_await file.write_exact(meta.dump(2) + "\n");
}

// Opens the capture a call asks for, or reports why it can't and returns nullopt.
coro::Coro<std::optional<Capture>> start(const CapturePoint& point, CaptureCall call) {
    const std::string base = (std::filesystem::path(call.dir) / call.request.name()).string();
    const std::string data_path = base + ".sigmf-data";
    const std::string meta_path = base + ".sigmf-meta";
    // Checked rather than opened exclusively, which coro::File can't do: only a second capture of the same
    // name racing this one gets past it.
    if (std::filesystem::exists(data_path) || std::filesystem::exists(meta_path)) {
        LOGF(WARNING, "capture %s: refused, %s exists", point.name, data_path);
        (void)co_await call.progress.send(
            failure(wsrpc::v1::Error::INVALID_ARGUMENT, data_path + " exists, and a capture never overwrites one"));
        co_return std::nullopt;
    }
    std::optional<coro::File> data;
    std::string failed;
    try {
        data.emplace(co_await coro::File::open(data_path, coro::FileMode::Write | coro::FileMode::Create));
    } catch (const std::exception& e) {
        failed = e.what();
    }
    if (!data) {
        LOGF(ERROR, "capture %s: can't create %s: %s", point.name, data_path, failed);
        (void)co_await call.progress.send(failure(wsrpc::v1::Error::INTERNAL, data_path + ": " + failed));
        co_return std::nullopt;
    }
    const auto samples =
        std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(call.request.duration_s() * point.rate_hz)));
    LOGF(INFO, "capture %s: started, %g s (%u samples at %g Hz) to %s", point.name, call.request.duration_s(),
         samples, point.rate_hz, data_path);
    co_return Capture{.progress = std::move(call.progress),
                      .name = call.request.name(),
                      .data_path = data_path,
                      .meta_path = meta_path,
                      .data = std::move(*data),
                      .samples = samples,
                      .start = std::nullopt,
                      .datatype = {}};
}

// Writes what of `item` falls in the capture. True once the capture is done: its duration covered, or the
// item's format not the capture's.
template <class Item>
coro::Coro<bool> write(Capture& c, const CapturePoint& point, const Item& item) {
    const uint64_t first = first_index(item);
    const uint64_t end_of_item = first + count(item);
    if (!c.start) {
        c.start = first;
        c.next = first;
        c.datatype = datatype(item);
    } else if (datatype(item) != c.datatype) {
        LOGF(WARNING, "capture %s: ended early, the input changed from %s to %s", point.name, c.datatype,
             datatype(item));
        co_return true;
    }
    const uint64_t end = *c.start + c.samples;
    // A SigMF capture segment starts at the first sample and after each gap.
    bool new_segment = c.segments.empty();
    if (first > c.next) {
        const uint64_t gap = std::min(first, end) - c.next;
        TRACE_PLOT(point.gap_plot, static_cast<int64_t>(gap));
        LOGF_EVERY_N_SEC(WARNING, 1, "capture %s: %u samples missing at index %u, behind a slow disk or lost by "
                         "the source; the recording carries on in a new SigMF capture segment",
                         point.name, gap, c.next);
        c.next = std::min(first, end);
        if (c.next == end) co_return true;
        new_segment = true;
    }
    if (end_of_item <= c.next) co_return false;
    if (new_segment) {
        nlohmann::json segment = {{"core:sample_start", c.written}, {"core:global_index", c.next}};
        if (point.freq_hz) segment["core:frequency"] = *point.freq_hz;
        c.segments.push_back(std::move(segment));
        // Rewritten at each segment, so a capture cut short by Ctrl+C still has metadata for all it wrote.
        co_await write_meta(c, point);
    }
    const uint64_t take = std::min(end_of_item, end) - c.next;
    (void)co_await c.data.write_exact(bytes(item, c.next - first, take));
    c.written += take;
    c.next += take;
    co_return c.next == end;
}

// The capture's progress so far.
api::RecordCaptureProgress report_of(const Capture& c) {
    api::RecordCaptureProgress r;
    r.set_start_index(c.start.value_or(0));
    r.set_samples(c.written);
    r.set_missing(missing(c));
    return r;
}

// Writes the final metadata and sends the last report; `why` is for the log.
coro::Coro<void> finish(Capture& c, const CapturePoint& point, const char* why) {
    if (c.start) co_await write_meta(c, point);
    api::RecordCaptureProgress last = report_of(c);
    api::Capture& capture = *last.mutable_capture();
    capture.set_name(c.name);
    capture.set_data_path(c.data_path);
    capture.set_data_bytes(c.written * sample_bytes(c.datatype));
    capture.set_modified_unix_s(
        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count());
    if (c.start) {
        api::CaptureMeta& meta = *capture.mutable_meta();
        meta.set_datatype(c.datatype);
        meta.set_rate_hz(point.rate_hz);
        if (point.freq_hz) meta.set_freq_hz(*point.freq_hz);
        meta.set_description(description(point));
        meta.set_duration_s(duration_s(capture.data_bytes(), meta));
    }
    LOGF(INFO, "capture %s: %s, %u samples (%.3f s) in %s, %u missing", point.name, why, c.written,
         static_cast<double>(c.written) / point.rate_hz, c.data_path, last.missing());
    // Waits for room, since it carries the result. Fails only for a cancelled call, which has no one to tell.
    (void)co_await c.progress.send(std::move(last));
}

// The loop both capture tasks run. Writing holds the task back from `in`, whose sender drops what doesn't fit,
// so a slow disk loses samples rather than slowing the pipeline.
template <class Item>
coro::Coro<void> run_captures(CapturePoint point, coro::MpscReceiver<Item> in,
                              coro::MpscReceiver<CaptureCall> calls, coro::WatchSender<bool>* active_flag) {
    std::optional<Capture> active;
    auto set_active = [&](bool on) {
        if (active_flag) (void)active_flag->send(on);
    };
    bool calls_open = true;
    std::string failed;
    for (;;) {
        auto woke = co_await coro::select(in.recv(), coro::when(calls_open, [&] { return calls.recv(); }));
        if (woke.index() == 1) {
            auto& call = std::get<1>(woke).value;
            if (!call) {
                calls_open = false;
            } else if (active) {
                (void)co_await call->progress.send(failure(
                    wsrpc::v1::Error::RESOURCE_EXHAUSTED, std::string("a capture of the ") + point.name +
                                                              " is in progress"));
            } else {
                active = co_await start(point, std::move(*call));
                if (active) set_active(true);
            }
            continue;
        }
        auto& item = std::get<0>(woke).value;
        if (!item) break;
        if (!active) continue;
        const char* why = nullptr;
        try {
            if (active->progress.is_closed()) {
                why = "cancelled";
            } else if (co_await write(*active, point, *item)) {
                why = "done";
            } else if (const auto now = std::chrono::steady_clock::now(); now >= active->next_report) {
                // Dropped when the server hasn't taken the last two: the next one says as much.
                (void)active->progress.try_send(report_of(*active));
                active->next_report = now + kReportInterval;
            }
            if (why) co_await finish(*active, point, why);
        } catch (const std::exception& e) {
            failed = e.what();
        }
        if (!failed.empty()) {
            LOGF(ERROR, "capture %s: failed writing %s: %s", point.name, active->data_path, failed);
            (void)co_await active->progress.send(
                failure(wsrpc::v1::Error::INTERNAL, active->data_path + ": " + failed));
            failed.clear();
            why = "failed";
        }
        if (why) {
            active.reset();
            set_active(false);
        }
    }
    if (active) {
        try {
            co_await finish(*active, point, "ended early with the input");
        } catch (const std::exception& e) {
            failed = e.what();
        }
        if (!failed.empty()) LOGF(ERROR, "capture %s: failed writing %s: %s", point.name, active->meta_path, failed);
        set_active(false);
    }
}

}  // namespace

coro::Coro<void> capture_input(CapturePoint point, coro::MpscReceiver<IqBlock> in,
                               coro::MpscReceiver<CaptureCall> calls) {
    co_await run_captures(point, std::move(in), std::move(calls), nullptr);
}

coro::Coro<void> capture_envelope(CapturePoint point, coro::MpscReceiver<EnvelopeRun> in,
                                  coro::MpscReceiver<CaptureCall> calls, coro::WatchSender<bool> active) {
    co_await run_captures(point, std::move(in), std::move(calls), &active);
}

std::expected<std::string, std::string> resolve_capture_dir(const std::string& dir) {
    std::error_code ec;
    const std::filesystem::path path = std::filesystem::absolute(dir, ec).lexically_normal();
    if (ec) return std::unexpected(dir + ": " + ec.message());
    if (!std::filesystem::is_directory(path, ec)) return std::unexpected(dir + " is not a directory");
    if (::access(path.c_str(), W_OK | X_OK) != 0) return std::unexpected(dir + " is not writable");
    std::string resolved = path.string();
    // lexically_normal() keeps the trailing '/' of "dir/", which would make the same directory two settings.
    if (resolved.size() > 1 && resolved.back() == '/') resolved.pop_back();
    return resolved;
}

std::expected<api::ListCapturesResponse, std::string> list_capture_files(const std::string& dir) {
    namespace fs = std::filesystem;
    struct Found {
        fs::path data;
        uint64_t bytes;
        fs::file_time_type modified;
    };
    std::vector<Found> found;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".sigmf-data") continue;
        // A file that goes, or can't be read, between listing and looking isn't listed.
        std::error_code file_ec;
        if (!it->is_regular_file(file_ec)) continue;
        const uint64_t bytes = it->file_size(file_ec);
        if (file_ec) continue;
        const fs::file_time_type modified = it->last_write_time(file_ec);
        if (file_ec) continue;
        found.push_back({it->path(), bytes, modified});
    }
    if (ec) return std::unexpected(dir + ": " + ec.message());
    std::ranges::sort(found, std::greater{}, &Found::modified);

    api::ListCapturesResponse out;
    out.set_dir(dir);
    const size_t listed = std::min(found.size(), kMaxListedFiles);
    out.set_not_listed(static_cast<uint32_t>(found.size() - listed));
    for (size_t i = 0; i < listed; i++) {
        api::Capture& f = *out.add_captures();
        f.set_name(found[i].data.stem().string());
        f.set_data_path(found[i].data.string());
        f.set_data_bytes(found[i].bytes);
        const auto modified = std::chrono::file_clock::to_sys(found[i].modified);
        f.set_modified_unix_s(std::chrono::duration<double>(modified.time_since_epoch()).count());
        read_meta(f, fs::path(found[i].data).replace_extension(".sigmf-meta"));
    }
    return out;
}
