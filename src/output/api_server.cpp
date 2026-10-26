#include "output/api_server.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/defer.h>
#include <coro/io/ws_listener.h>
#include <coro/io/ws_stream.h>
#include <coro/stream.h>
#include <coro/sync/interval.h>
#include <coro/sync/select.h>
#include <coro/sync/when.h>
#include <coro/task/join_set.h>
#include <coro/task/spawn_blocking.h>
#include <coro/task/spawn_builder.h>
#include <google/protobuf/util/message_differencer.h>

#include "adsb/api/v1/api.wsrpc.h"
#include "common/log.h"
#include "common/trace.h"
#include "wsrpc/serve.h"

namespace {

using google::protobuf::util::MessageDifferencer;
using wsrpc::CallError;
using Error = wsrpc::v1::Error;

// Blocks' frames, or spectrum rows, a broadcast holds for a call that hasn't taken them yet.
constexpr size_t kLiveQueueItems = 64;

// Capture calls on their way to the pipeline, from all the connections.
constexpr size_t kCaptureQueue = 4;

// Every call runs in a task of its own, so a handler takes its own clone of anything that is for one task
// at a time.
class ReceiverHandlers final : public api::Receiver {
public:
    // Shortest time between a watch's replies; a request for less gets this.
    static constexpr double kMinIntervalS = 0.1;
    // Longest time between a watch's replies; a request for more is refused, as it would overflow.
    static constexpr double kMaxIntervalS = 3600;

    ReceiverHandlers(ApiReceivers live, api::ReceiverInfo info) : m_live(std::move(live)), m_info(std::move(info)) {}

    coro::Coro<api::ReceiverInfo> get_receiver_info(const api::GetReceiverInfoRequest&) override { co_return m_info; }

    coro::CoroStream<api::FrameBatch> watch_frames(const api::WatchFramesRequest& request) override {
        auto frames = m_live.frames.resubscribe();
        coro::IntervalTimer timer(interval(request));
        api::FrameBatch batch;
        for (;;) {
            // While the client is slow, this waits at its co_yield and stops receiving, so the broadcast's ring
            // fills and the next recv() reports what was skipped.
            auto woke = co_await coro::select(frames.recv(), timer.tick());
            if (woke.index() == 0) {
                auto& item = std::get<0>(woke).value;
                if (!item) {
                    if (item.error().kind != coro::BroadcastRecvError::Kind::Lagged) co_return;
                    TRACE_PLOT("ws frames dropped", static_cast<int64_t>(item.error().skipped));
                    LOGF(WARNING, "websocket client lagged: dropped %u block(s) of frames", item.error().skipped);
                    batch.set_frames_dropped(true);
                    continue;
                }
                for (const api::Frame& f : **item) *batch.add_frames() = f;
                continue;
            }
            if (batch.frames_size() > 0 || batch.frames_dropped()) co_yield std::exchange(batch, {});
        }
    }

    coro::CoroStream<api::AircraftChanges> watch_aircraft(const api::WatchAircraftRequest& request) override {
        auto aircraft = m_live.aircraft.clone();
        coro::IntervalTimer timer(interval(request));
        // Copies, so no watch lock is held past the statement.
        LiveAircraft sent = *aircraft.borrow_and_update();
        // Every one at once, even none, so the client knows where it starts.
        api::AircraftChanges changes;
        for (const auto& [icao, ac] : sent) *changes.add_aircraft() = ac;
        co_yield std::exchange(changes, {});
        for (;;) {
            co_await timer.tick();
            {
                TRACE_ZONE("ws_aircraft_changes");
                LiveAircraft now = *aircraft.borrow_and_update();
                for (const auto& [icao, ac] : now) {
                    auto it = sent.find(icao);
                    if (it == sent.end() || !MessageDifferencer::Equals(it->second, ac)) *changes.add_aircraft() = ac;
                }
                sent = std::move(now);
            }
            if (changes.aircraft_size() > 0) co_yield std::exchange(changes, {});
        }
    }

    coro::CoroStream<api::InputStatus> watch_input_status(const api::WatchInputStatusRequest&) override {
        auto status = m_live.input_status.clone();
        for (;;) {
            std::optional<api::InputStatus> now = *status.borrow_and_update();
            if (now) co_yield std::move(*now);
            if (!co_await status.changed()) co_return;
        }
    }

    // While this is in progress, the pipeline computes the spectrum rows it sends.
    coro::CoroStream<api::SpectrumRow> watch_spectrum(const api::WatchSpectrumRequest&) override {
        auto rows = m_live.spectrum.resubscribe();
        coro::WatchSender<PipelineDemand> demand = m_live.demand.clone();
        demand.send_if_modified([](PipelineDemand& d) {
            ++d.spectrum_viewers;
            return true;
        });
        // However the call ends, its frame is destroyed, so the count comes down.
        coro::Defer uncount([&demand] {
            demand.send_if_modified([](PipelineDemand& d) {
                --d.spectrum_viewers;
                return true;
            });
        });
        for (;;) {
            // As in watch_frames(), a slow client holds this at its co_yield until the ring overtakes it.
            auto row = co_await rows.recv();
            if (!row) {
                if (row.error().kind != coro::BroadcastRecvError::Kind::Lagged) co_return;
                TRACE_PLOT("ws spectrum dropped", static_cast<int64_t>(row.error().skipped));
                LOGF(WARNING, "websocket spectrum client lagged: dropped %u row(s)", row.error().skipped);
                continue;
            }
            co_yield **row;
        }
    }

    coro::CoroStream<api::Settings> watch_settings(const api::WatchSettingsRequest&) override {
        auto settings = m_live.settings.rx.clone();
        for (;;) {
            api::Settings now = *settings.borrow_and_update();
            co_yield std::move(now);
            // m_live holds a sender, so this waits until the client cancels the call.
            if (!co_await settings.changed()) co_return;
        }
    }

    // Checks every field the request sets before changing any.
    coro::Coro<api::Settings> update_settings(const api::UpdateSettingsRequest& request) override {
        std::optional<std::string> dir;
        if (request.has_capture_dir()) {
            dir.emplace();
            if (!request.capture_dir().empty()) {
                auto resolved = resolve_capture_dir(request.capture_dir());
                if (!resolved) throw CallError(Error::INVALID_ARGUMENT, "capture_dir: " + resolved.error());
                *dir = std::move(*resolved);
            }
        }
        coro::WatchSender<api::Settings> tx = m_live.settings.tx.clone();
        api::Settings after;
        bool dir_changed = false;
        tx.send_if_modified([&](api::Settings& s) {
            if (dir && s.capture_dir() != *dir) {
                s.set_capture_dir(*dir);
                dir_changed = true;
            }
            after = s;
            return dir_changed;
        });
        if (dir_changed && dir->empty())
            LOGF(INFO, "capture: off, turned off by a client");
        else if (dir_changed)
            LOGF(INFO, "capture: on, into %s, set by a client", *dir);
        co_return after;
    }

    // Reported by the pipeline's capture task for the point; a cancel closes the reports' channel, which ends the
    // capture.
    coro::CoroStream<api::RecordCaptureProgress> record_capture(const api::RecordCaptureRequest& request) override {
        std::string dir = capture_dir();
        if (request.point() != api::INPUT && request.point() != api::ENVELOPE)
            throw CallError(Error::INVALID_ARGUMENT, "no such capture point");
        const double s = request.duration_s();
        if (!(s > 0 && s <= CaptureCall::kMaxDurationS)) {  // NaN too
            throw CallError(Error::INVALID_ARGUMENT, "duration_s " + std::to_string(s) +
                                                         " isn't over 0 and at most " +
                                                         std::to_string(CaptureCall::kMaxDurationS));
        }
        const std::string& name = request.name();
        if (name.empty() || name.front() == '.' || name.find_first_of(std::string_view("/\0", 2)) != name.npos)
            throw CallError(Error::INVALID_ARGUMENT, "name must be a file name, not starting with '.'");
        auto [reports_tx, reports] = coro::mpsc_channel<CaptureCall::Report>(CaptureCall::kProgressQueue);
        coro::MpscSender<CaptureCall> captures = m_live.captures.clone();
        if (!co_await captures.send(CaptureCall{request, std::move(dir), std::move(reports_tx)}))
            throw CallError(Error::INTERNAL, "the pipeline has stopped taking captures");
        for (;;) {
            auto report = co_await reports.recv();
            if (!report) throw CallError(Error::INTERNAL, "the capture task ended without its last report");
            if (!*report) throw report->error();
            const bool last = (*report)->has_capture();
            co_yield std::move(**report);
            if (last) co_return;
        }
    }

    coro::Coro<api::ListCapturesResponse> list_captures(const api::ListCapturesRequest&) override {
        auto listed = co_await coro::spawn_blocking([dir = capture_dir()] { return list_capture_files(dir); });
        if (!listed) {
            LOGF(WARNING, "capture files: can't list them: %s", listed.error());
            throw CallError(Error::INTERNAL, listed.error());
        }
        co_return std::move(*listed);
    }

private:
    // The interval of a watch's request.
    template<typename Request>
    std::chrono::nanoseconds interval(const Request& request) const {
        double s = request.has_interval_s() ? request.interval_s() : m_info.default_interval_s();
        if (!(s <= kMaxIntervalS)) {  // NaN too
            throw CallError(Error::INVALID_ARGUMENT,
                            "interval_s " + std::to_string(s) + " is over " + std::to_string(kMaxIntervalS));
        }
        if (s < kMinIntervalS) {
            LOGF(INFO, "websocket watch: interval %g s raised to %g s", s, kMinIntervalS);
            s = kMinIntervalS;
        }
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(s));
    }

    // The capture directory, for a capture call: empty is captures off.
    std::string capture_dir() const {
        std::string dir = m_live.settings.rx.clone().get().capture_dir();
        if (dir.empty()) throw CallError(Error::FAILED_PRECONDITION, "captures are off: no capture directory is set");
        return dir;
    }

    ApiReceivers m_live;
    api::ReceiverInfo m_info;
};

class HistoryHandlers final : public api::History {
public:
    explicit HistoryHandlers(AircraftHistory db) : m_db(std::move(db)) {}

    // A query that returns nothing failed, and the history task logged why.

    coro::Coro<api::Snapshot> get_snapshot(const api::GetSnapshotRequest& request) override {
        AircraftHistory db = m_db.clone();
        auto reply = co_await db.snapshot(request);
        if (!reply) throw CallError(Error::INTERNAL, "the history query failed");
        co_return std::move(*reply);
    }

    coro::Coro<api::AircraftHistory> get_aircraft_history(const api::GetAircraftHistoryRequest& request) override {
        AircraftHistory db = m_db.clone();
        auto reply = co_await db.aircraft(request);
        if (!reply) throw CallError(Error::INTERNAL, "the history query failed");
        co_return std::move(*reply);
    }

    coro::Coro<api::Activity> get_activity(const api::GetActivityRequest& request) override {
        AircraftHistory db = m_db.clone();
        auto reply = co_await db.activity(request);
        if (!reply) throw CallError(Error::INTERNAL, "the history query failed");
        co_return std::move(*reply);
    }

    coro::Coro<api::Heatmap> get_heatmap(const api::GetHeatmapRequest& request) override {
        if (!(request.east() > request.west()) || !(request.north() > request.south())) {  // NaN too
            throw CallError(Error::INVALID_ARGUMENT,
                            "the heatmap's area is empty: east must be over west, and north over south");
        }
        AircraftHistory db = m_db.clone();
        auto reply = co_await db.heatmap(request);
        if (!reply) throw CallError(Error::INTERNAL, "the history query failed");
        co_return std::move(*reply);
    }

private:
    AircraftHistory m_db;
};

// One client's connection, serving the services the server has. The handlers are locals of this frame, so they
// outlive serve().
coro::Coro<void> serve_client(coro::WsStream ws, std::optional<ApiReceivers> live, api::ReceiverInfo info,
                              std::optional<AircraftHistory> history) {
    std::optional<ReceiverHandlers> receiver;
    std::optional<HistoryHandlers> history_handlers;
    std::vector<wsrpc::Service*> services;
    if (live) services.push_back(&receiver.emplace(std::move(*live), std::move(info)));
    if (history) services.push_back(&history_handlers.emplace(std::move(*history)));
    co_await wsrpc::serve(std::move(ws), std::move(services));
}

coro::Coro<void> run(uint16_t port, std::optional<ApiReceivers> live, api::ReceiverInfo info,
                     std::optional<AircraftHistory> history) {
    coro::WsListener listener = co_await coro::WsListener::bind("0.0.0.0", port);
    std::string serves;
    if (live) {
        const std::string dir = live->settings.rx.get().capture_dir();
        serves = "Receiver from " + info.source() + ", " +
                 (dir.empty() ? "captures off until a client sets a directory" : "captures into " + dir);
    }
    if (history) serves += serves.empty() ? "History" : "; History";
    LOGF(INFO, "websocket listening on port %u: a watch's reply every %g s unless its call asks otherwise; "
         "serving %s",
         port, info.default_interval_s(), serves);
    // Runs until the task is cancelled: Ctrl+C is handled in main(), and ~Runtime cancels and drains this
    // task (see api_server_spawn()). JoinSet's destructor then cancels and drains any remaining clients.
    coro::JoinSet<void> clients;
    for (;;) {
        try {
            // when() skips the JoinSet until it has a client: next() on an empty one resolves at once.
            auto result = co_await coro::select(listener.accept(),
                                                coro::when(!clients.empty(), [&] { return coro::next(clients); }));
            if (result.index() == 0) {
                std::optional<ApiReceivers> client_live;
                if (live) client_live = live->clone();
                std::optional<AircraftHistory> client_history;
                if (history) client_history = history->clone();
                clients
                    .build_task(serve_client(std::move(std::get<0>(result).value), std::move(client_live), info,
                                             std::move(client_history)))
                    .name("ws_client")
                    .spawn();
                LOGF(INFO, "websocket client connected");
            } else {
                LOGF(INFO, "websocket client disconnected");
            }
        } catch (const std::exception& e) {
            LOGF(ERROR, "websocket: %s", e.what());
            co_return;
        }
    }
}

}  // namespace

ApiReceivers ApiReceivers::clone() const {
    return {frames.resubscribe(), spectrum.resubscribe(), aircraft.clone(), input_status.clone(),
            demand.clone(),       settings.clone(),       captures.clone()};
}

std::pair<ApiSenders, ApiReceivers> api_channels(api::Settings settings) {
    auto [frames_tx, frames_rx] = coro::broadcast_channel<LiveFrames>(kLiveQueueItems);
    auto [spectrum_tx, spectrum_rx] = coro::broadcast_channel<LiveSpectrumRow>(kLiveQueueItems);
    auto [aircraft_tx, aircraft_rx] = coro::watch_channel(LiveAircraft{});
    auto [status_tx, status_rx] = coro::watch_channel(std::optional<api::InputStatus>{});
    auto [demand_tx, demand_rx] = coro::watch_channel(PipelineDemand{});
    auto [settings_tx, settings_rx] = coro::watch_channel(std::move(settings));
    auto [captures_tx, captures_rx] = coro::mpsc_channel<CaptureCall>(kCaptureQueue);
    ApiSenders senders{.frames = std::move(frames_tx),
                       .spectrum = std::move(spectrum_tx),
                       .aircraft = std::move(aircraft_tx),
                       .input_status = std::move(status_tx),
                       .demand = std::move(demand_rx),
                       .settings = settings_rx.clone(),
                       .captures = std::move(captures_rx)};
    ApiReceivers receivers{.frames = std::move(frames_rx),
                           .spectrum = std::move(spectrum_rx),
                           .aircraft = std::move(aircraft_rx),
                           .input_status = std::move(status_rx),
                           .demand = std::move(demand_tx),
                           .settings = {std::move(settings_tx), std::move(settings_rx)},
                           .captures = std::move(captures_tx)};
    return {std::move(senders), std::move(receivers)};
}

void api_server_spawn(uint16_t port, std::chrono::nanoseconds default_interval, std::optional<ApiReceivers> live,
                      std::string source, std::optional<AircraftHistory> history) {
    api::ReceiverInfo info;
    info.set_source(std::move(source));
    info.set_default_interval_s(std::chrono::duration<double>(default_interval).count());
    info.set_max_capture_s(CaptureCall::kMaxDurationS);
    // Detached: run() accepts clients until the runtime's destructor cancels and drains it.
    coro::build_task()
        .name("ws_accept")
        .spawn(run(port, std::move(live), std::move(info), std::move(history)))
        .detach();
}
