#include "output/capture.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <coro/coro_stream.h>
#include <coro/io/file.h>
#include <coro/sync/select.h>
#include <coro/sync/when.h>
#include <nlohmann/json.hpp>

#include "common/log.h"
#include "common/trace.h"

namespace {

static_assert(std::endian::native == std::endian::little, "cf32_le and rf32_le are written as the host's floats");

CaptureCall::Reply failure(api::Error::Code code, std::string message) {
    api::Error e;
    e.set_code(code);
    e.set_message(std::move(message));
    return std::unexpected(std::move(e));
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
    coro::OneshotSender<CaptureCall::Reply> reply;
    std::string data_path;
    std::string meta_path;
    coro::File data;
    uint64_t samples;                  // to cover, in stream time
    std::optional<uint64_t> start;     // stream index of the first sample, once the first item has arrived
    uint64_t next = 0;                 // stream index of the next sample to write
    uint64_t written = 0;
    std::string datatype;
    nlohmann::json segments = nlohmann::json::array();  // SigMF captures, one per run without a gap
};

// The stream time a capture has covered that isn't in its file.
uint64_t missing(const Capture& c) { return c.start ? c.next - *c.start - c.written : 0; }

coro::Coro<void> write_meta(const Capture& c, const CapturePoint& point) {
    nlohmann::json global = {
        {"core:datatype", c.datatype},
        {"core:sample_rate", point.rate_hz},
        {"core:version", "1.0.0"},
        {"core:recorder", "adsb"},
        {"core:description", std::string("adsb capture of the ") + point.name},
    };
    const nlohmann::json meta = {{"global", std::move(global)}, {"captures", c.segments},
                                 {"annotations", nlohmann::json::array()}};
    coro::File file = co_await coro::File::open(c.meta_path, coro::FileMode::Write | coro::FileMode::Create |
                                                                 coro::FileMode::Truncate);
    (void)co_await file.write_exact(meta.dump(2) + "\n");
}

// Opens the capture a call asks for, or replies with why it can't and returns nullopt.
coro::Coro<std::optional<Capture>> start(const std::string& dir, const CapturePoint& point, CaptureCall call) {
    const std::string base = (std::filesystem::path(dir) / call.request.name()).string();
    const std::string data_path = base + ".sigmf-data";
    const std::string meta_path = base + ".sigmf-meta";
    // Checked rather than opened exclusively, which coro::File can't do: only a second capture of the same
    // name racing this one gets past it.
    if (std::filesystem::exists(data_path) || std::filesystem::exists(meta_path)) {
        LOGF(WARNING, "capture %s: refused, %s exists", point.name, data_path);
        (void)call.reply.send(
            failure(api::Error::INVALID_ARGUMENT, data_path + " exists, and a capture never overwrites one"));
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
        (void)call.reply.send(failure(api::Error::INTERNAL, data_path + ": " + failed));
        co_return std::nullopt;
    }
    const auto samples =
        std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(call.request.duration_s() * point.rate_hz)));
    LOGF(INFO, "capture %s: started, %g s (%u samples at %g Hz) to %s", point.name, call.request.duration_s(),
         samples, point.rate_hz, data_path);
    co_return Capture{.reply = std::move(call.reply),
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

// Writes the final metadata and replies; `why` is for the log.
coro::Coro<void> finish(Capture& c, const CapturePoint& point, const char* why) {
    if (c.start) co_await write_meta(c, point);
    api::CaptureResult result;
    result.set_data_path(c.data_path);
    result.set_datatype(c.datatype);
    result.set_rate_hz(point.rate_hz);
    result.set_start_index(c.start.value_or(0));
    result.set_samples(c.written);
    result.set_missing(missing(c));
    LOGF(INFO, "capture %s: %s, %u samples (%.3f s) in %s, %u missing", point.name, why, c.written,
         static_cast<double>(c.written) / point.rate_hz, c.data_path, result.missing());
    // Fails only for a cancelled call, which has no one to tell.
    (void)c.reply.send(std::move(result));
}

// The loop both capture tasks run. Writing holds the task back from `in`, whose sender drops what doesn't fit,
// so a slow disk loses samples rather than slowing the pipeline.
template <class Item>
coro::Coro<void> run_captures(std::string dir, CapturePoint point, coro::MpscReceiver<Item> in,
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
                (void)call->reply.send(failure(api::Error::RESOURCE_EXHAUSTED,
                                               std::string("a capture of the ") + point.name + " is in progress"));
            } else {
                active = co_await start(dir, point, std::move(*call));
                if (active) set_active(true);
            }
            continue;
        }
        auto& item = std::get<0>(woke).value;
        if (!item) break;
        if (!active) continue;
        const char* why = nullptr;
        try {
            if (active->reply.is_closed()) why = "cancelled";
            else if (co_await write(*active, point, *item)) why = "done";
            if (why) co_await finish(*active, point, why);
        } catch (const std::exception& e) {
            failed = e.what();
        }
        if (!failed.empty()) {
            LOGF(ERROR, "capture %s: failed writing %s: %s", point.name, active->data_path, failed);
            (void)active->reply.send(failure(api::Error::INTERNAL, active->data_path + ": " + failed));
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

coro::Coro<void> capture_input(std::string dir, CapturePoint point, coro::MpscReceiver<IqBlock> in,
                               coro::MpscReceiver<CaptureCall> calls) {
    co_await run_captures(std::move(dir), point, std::move(in), std::move(calls), nullptr);
}

coro::Coro<void> capture_envelope(std::string dir, CapturePoint point, coro::MpscReceiver<EnvelopeRun> in,
                                  coro::MpscReceiver<CaptureCall> calls, coro::WatchSender<bool> active) {
    co_await run_captures(std::move(dir), point, std::move(in), std::move(calls), &active);
}
