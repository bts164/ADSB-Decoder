#include "output/api_server.h"

#include <cstddef>
#include <exception>
#include <span>
#include <stdexcept>
#include <string>

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/io/ws_listener.h>
#include <coro/io/ws_stream.h>
#include <coro/stream.h>
#include <coro/sync/interval.h>
#include <coro/sync/join.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/oneshot.h>
#include <coro/sync/select.h>
#include <coro/sync/timeout.h>
#include <coro/sync/when.h>
#include <coro/task/join_handle.h>
#include <coro/task/join_set.h>
#include <coro/task/spawn_builder.h>
#include <google/protobuf/util/message_differencer.h>

#include "common/log.h"
#include "common/trace.h"

namespace {

using google::protobuf::util::MessageDifferencer;

// Blocks' frames and spectrum rows the live channel holds for a call that hasn't taken them yet.
constexpr size_t kLiveQueueItems = 64;

// Client messages read from the socket and not yet routed.
constexpr size_t kReadQueue = 4;

// Serialized messages waiting for the connection's writer, from all its calls.
constexpr size_t kWriteQueue = 16;

// A send that takes longer closes the connection. Long enough never to fire on a client that's just slow:
// a message isn't dropped instead, since a dropped `end` would leave the client waiting forever.
constexpr auto kSendTimeout = std::chrono::seconds(20);

// A call's failure, thrown by a handler and sent to the client as an Error with this code.
class CallError : public std::runtime_error {
public:
    CallError(api::Error::Code code, const std::string& message) : std::runtime_error(message), m_code(code) {}
    api::Error::Code code() const { return m_code; }

private:
    api::Error::Code m_code;
};

api::ServerMessage error_message(uint32_t id, api::Error::Code code, std::string message) {
    api::ServerMessage msg;
    msg.set_id(id);
    msg.set_end(true);
    api::Error& error = *msg.mutable_error();
    error.set_code(code);
    error.set_message(std::move(message));
    return msg;
}

// A CallError keeps its code; anything else is a bug in the handler, so it's logged and INTERNAL.
void set_error(api::ServerMessage& msg, std::exception_ptr e) {
    msg.clear_body();
    api::Error& error = *msg.mutable_error();
    try {
        std::rethrow_exception(e);
    } catch (const CallError& c) {
        error.set_code(c.code());
        error.set_message(c.what());
    } catch (const std::exception& x) {
        LOGF(ERROR, "websocket call %u failed: %s", msg.id(), x.what());
        error.set_code(api::Error::INTERNAL);
        error.set_message(x.what());
    } catch (...) {
        LOGF(ERROR, "websocket call %u failed with an unknown exception", msg.id());
        error.set_code(api::Error::INTERNAL);
        error.set_message("unknown exception");
    }
}

// Serializes `msg` and queues it for the writer, waiting for room. False if the writer is gone.
coro::Coro<bool> send(coro::MpscSender<std::string>& out, const api::ServerMessage& msg) {
    VLOGF(2, "websocket client sent: %s", msg.ShortDebugString());
    std::string bytes;
    {
        TRACE_ZONE("ws_serialize");
        bytes = msg.SerializeAsString();
    }
    co_return (co_await out.send(std::move(bytes))).has_value();
}

// The reply's place in the envelope; a reply type without one doesn't compile.
void put(api::ServerMessage& m, api::ServerInfo r) { *m.mutable_server_info() = std::move(r); }
void put(api::ServerMessage& m, api::Update r) { *m.mutable_update() = std::move(r); }
void put(api::ServerMessage& m, api::SpectrumRow r) { *m.mutable_spectrum() = std::move(r); }
void put(api::ServerMessage& m, api::Snapshot r) { *m.mutable_snapshot() = std::move(r); }
void put(api::ServerMessage& m, api::AircraftHistory r) { *m.mutable_aircraft_history() = std::move(r); }
void put(api::ServerMessage& m, api::Activity r) { *m.mutable_activity() = std::move(r); }
void put(api::ServerMessage& m, api::Heatmap r) { *m.mutable_heatmap() = std::move(r); }

bool has_anything(const api::Update& update) {
    return update.frames_size() > 0 || update.aircraft_size() > 0 || update.has_input_status();
}

// Counts one spectrum call in PipelineSettings::spectrum_viewers, from its construction to its destruction. A
// local of the call's handler, so the count comes down however the call ends: each way destroys its frame.
class SpectrumViewer {
public:
    explicit SpectrumViewer(coro::WatchSender<PipelineSettings> settings) : m_settings(std::move(settings)) {
        m_settings.send_if_modified([](PipelineSettings& s) {
            ++s.spectrum_viewers;
            return true;
        });
    }
    ~SpectrumViewer() {
        m_settings.send_if_modified([](PipelineSettings& s) {
            --s.spectrum_viewers;
            return true;
        });
    }
    SpectrumViewer(const SpectrumViewer&) = delete;
    SpectrumViewer& operator=(const SpectrumViewer&) = delete;

private:
    coro::WatchSender<PipelineSettings> m_settings;
};

/**
 * One client's connection: its calls, each answered by the Handle() overload for its request type.
 *
 * Each call runs in a task of its own (run_call()), which sends every message of the call, `end` last, on
 * `m_out`. Only that task sends its `end`, so a call gets exactly one: a cancel only signals the task.
 * Since the calls run at once, a handler takes its own clone of anything that is for one task at a time.
 *
 * The calls' tasks use this object, so it must outlive run(): see handle_connection().
 */
class ApiConnection {
public:
    struct Settings {
        std::chrono::nanoseconds default_interval;  // --ws-interval
        bool live;                                  // whether a pipeline publishes on the channels
    };

    // Most calls one connection may have in progress at once.
    static constexpr size_t kClientCalls = 8;
    // Shortest time between a subscription's updates; a request for less gets this.
    static constexpr double kMinUpdateIntervalS = 0.1;
    // Longest time between a subscription's updates; a request for more is refused.
    static constexpr double kMaxUpdateIntervalS = 3600;

    ApiConnection(ApiReceivers live, std::optional<AircraftHistory> history, Settings settings,
                  coro::MpscSender<std::string> out)
        : m_live(std::move(live)), m_history(std::move(history)), m_settings(settings), m_out(std::move(out)) {
        auto [done_tx, done_rx] = coro::mpsc_channel<uint32_t>(kClientCalls);
        m_done_tx = std::move(done_tx);
        m_done_rx.emplace(std::move(done_rx));
    }

    /**
     * Reads each serialized ClientMessage from `in` and acts on it, until `in` closes. Calls still in
     * progress then are cancelled. A call's task reports on `m_done_rx` when it finishes, and is forgotten
     * then.
     */
    coro::Coro<void> run(coro::MpscReceiver<std::vector<std::byte>> in) {
        // A local, not a member: run()'s frame is destroyed however it ends, dropping each call's task,
        // which cancels it, while this object, which the tasks use, still exists.
        Calls calls;
        for (;;) {
            auto woke = co_await coro::select(in.recv(),
                                              coro::when(!calls.empty(), [&] { return m_done_rx->recv(); }));
            if (woke.index() == 1) {
                if (const auto& id = std::get<1>(woke).value) calls.erase(*id);
                continue;
            }
            auto& bytes = std::get<0>(woke).value;
            if (!bytes) co_return;
            api::ClientMessage msg;
            if (!msg.ParseFromArray(bytes->data(), static_cast<int>(bytes->size()))) {
                LOGF(WARNING, "ignoring a websocket client message that isn't a ClientMessage (%zu bytes)",
                     bytes->size());
                continue;
            }
            VLOGF(2, "websocket client sent: %s", msg.ShortDebugString());
            // An error reply waits here for room in `m_out`, holding the reader back; only a client that sends
            // bad calls faster than it reads replies is held back by it.
            if (auto reply = route(calls, msg)) co_await send(m_out, *reply);
        }
    }

private:
    struct Call {
        coro::JoinHandle<void> task;       // dropped on disconnect, cancelling the call
        coro::OneshotSender<void> cancel;  // sent by cancel()
    };
    using Calls = std::unordered_map<uint32_t, Call>;

    // Acts on one message: starts a call, or cancels one. Returns the reply to send for a call refused at once.
    std::optional<api::ServerMessage> route(Calls& calls, const api::ClientMessage& msg) {
        using api::ClientMessage;
        const uint32_t id = msg.id();
        // No default: a call added to the .proto and not routed here is a -Wswitch warning.
        switch (msg.call_case()) {
        case ClientMessage::kCancel: cancel(calls, id); return std::nullopt;
        case ClientMessage::kServerInfo: return serve(calls, id, msg.server_info());
        case ClientMessage::kSubscribe: return serve(calls, id, msg.subscribe());
        case ClientMessage::kSpectrum: return serve(calls, id, msg.spectrum());
        case ClientMessage::kSnapshot: return serve(calls, id, msg.snapshot());
        case ClientMessage::kAircraftHistory: return serve(calls, id, msg.aircraft_history());
        case ClientMessage::kActivity: return serve(calls, id, msg.activity());
        case ClientMessage::kHeatmap: return serve(calls, id, msg.heatmap());
        case ClientMessage::CALL_NOT_SET:  // a call newer than this server, or none at all
            return error_message(id, api::Error::UNIMPLEMENTED, "unknown call");
        }
        return error_message(id, api::Error::UNIMPLEMENTED, "unknown call");
    }

    // Starts the call `id` in a task of its own, with the adapter for its handler's kind: a handler that
    // returns a coro::Stream is server streaming, and any other is unary.
    template <typename Request>
    std::optional<api::ServerMessage> serve(Calls& calls, uint32_t id, const Request& request) {
        if (id <= m_last_id) {
            return error_message(id, api::Error::INVALID_ARGUMENT,
                                 "call id " + std::to_string(id) + " isn't larger than the last, " +
                                     std::to_string(m_last_id));
        }
        m_last_id = id;
        if (calls.size() >= kClientCalls) {
            return error_message(id, api::Error::RESOURCE_EXHAUSTED,
                                 std::to_string(kClientCalls) + " calls are already in progress");
        }
        using Result = decltype(Handle(request));
        coro::Coro<void> adapter = [&] {
            if constexpr (coro::Stream<Result>) return stream_call(id, request);
            else return unary_call(id, request);
        }();
        auto [cancel_tx, cancel_rx] = coro::oneshot_channel<void>();
        coro::JoinHandle<void> task =
            coro::build_task()
                .name("ws_call")
                .spawn(run_call(id, std::move(adapter), std::move(cancel_rx), m_out.clone(), m_done_tx->clone()));
        calls.emplace(id, Call{std::move(task), std::move(cancel_tx)});
        // A streaming call is something a client turned on, such as the spectrum; a unary call is one of many,
        // such as each seek of the history scrubber.
        const std::string args = request.ShortDebugString();
        const auto name = Request::descriptor()->name();
        if constexpr (coro::Stream<Result>)
            LOGF(INFO, "websocket call %u: %s started%s%s", id, name, args.empty() ? "" : ": ", args);
        else
            VLOGF(1, "websocket call %u: %s started%s%s", id, name, args.empty() ? "" : ": ", args);
        return std::nullopt;
    }

    // An id that isn't in progress is ignored: its `end` is already queued.
    static void cancel(Calls& calls, uint32_t id) {
        auto it = calls.find(id);
        if (it != calls.end()) (void)it->second.cancel.send();
    }

    // One call: its adapter, unless the client cancels it first. The adapter sends the call's `end`, unless
    // the cancel wins; then select() drops and drains the adapter, stopping the handler, before this sends
    // `end` with CANCELLED. Either way, nothing of the call is sent after its `end`.
    static coro::Coro<void> run_call(uint32_t id, coro::Coro<void> adapter, coro::OneshotReceiver<void> cancel,
                                     coro::MpscSender<std::string> out, coro::MpscSender<uint32_t> done) {
        auto won = co_await coro::select(std::move(adapter), cancel.recv());
        if (won.index() == 1) {
            // The cancel's sender is only dropped without sending on disconnect, when this task is dropped too.
            if (!std::get<1>(won).value) co_return;
            co_await send(out, error_message(id, api::Error::CANCELLED, "cancelled"));
            LOGF(INFO, "websocket call %u: cancelled", id);
        }
        (void)co_await done.send(id);
    }

    // `request` lives in this frame, so the handler can keep a reference to it.
    template <typename Request>
    coro::Coro<void> unary_call(uint32_t id, Request request) {
        coro::MpscSender<std::string> out = m_out.clone();
        api::ServerMessage reply;
        reply.set_id(id);
        reply.set_end(true);
        try {
            put(reply, co_await Handle(request));
        } catch (...) {
            set_error(reply, std::current_exception());
        }
        co_await send(out, reply);
        VLOGF(1, "websocket call %u: ended%s", id, reply.has_error() ? " with an error" : "");
    }

    // Each item goes out before the handler is asked for the next, so a client that reads slowly holds the
    // handler back at its co_yield.
    template <typename Request>
    coro::Coro<void> stream_call(uint32_t id, Request request) {
        coro::MpscSender<std::string> out = m_out.clone();
        api::ServerMessage last;
        last.set_id(id);
        last.set_end(true);
        try {
            auto stream = Handle(request);
            while (auto item = co_await coro::next(stream)) {
                api::ServerMessage msg;
                msg.set_id(id);
                put(msg, std::move(*item));
                if (!co_await send(out, msg)) co_return;
            }
        } catch (...) {
            set_error(last, std::current_exception());
        }
        co_await send(out, last);
        LOGF(INFO, "websocket call %u: ended%s", id, last.has_error() ? " with an error" : "");
    }

    // --- The handlers, one per request type in ClientMessage. A handler fails by throwing a CallError. ---

    coro::Coro<api::ServerInfo> Handle(const api::ServerInfoRequest&) {
        api::ServerInfo info;
        info.set_history(m_history.has_value());
        info.set_spectrum(m_settings.live);
        info.set_default_interval_s(std::chrono::duration<double>(m_settings.default_interval).count());
        co_return info;
    }

    coro::CoroStream<api::Update> Handle(const api::SubscribeRequest& request) {
        std::chrono::nanoseconds interval = m_settings.default_interval;
        if (request.has_interval_s()) {
            double s = request.interval_s();
            if (!(s <= kMaxUpdateIntervalS)) {  // NaN too
                throw CallError(api::Error::INVALID_ARGUMENT, "interval_s " + std::to_string(s) + " is over " +
                                                                  std::to_string(kMaxUpdateIntervalS));
            }
            if (s < kMinUpdateIntervalS) {
                LOGF(INFO, "websocket subscription: interval %g s raised to %g s", s, kMinUpdateIntervalS);
                s = kMinUpdateIntervalS;
            }
            interval = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(s));
        }

        auto live = m_live.live.resubscribe();
        auto aircraft = m_live.aircraft.clone();
        auto status = m_live.input_status.clone();
        coro::IntervalTimer timer(interval);
        // What this subscription has been sent; it starts with nothing, so the first update has every aircraft.
        LiveAircraft sent_aircraft;
        std::optional<api::InputStatus> sent_status;
        api::Update update;
        for (;;) {
            // While the client is slow, this waits at its co_yield and stops receiving, so the broadcast's ring
            // fills and the next recv() reports what was skipped.
            auto woke = co_await coro::select(live.recv(), timer.tick());
            if (woke.index() == 0) {
                auto& item = std::get<0>(woke).value;
                if (!item) {
                    if (item.error().kind != coro::BroadcastRecvError::Kind::Lagged) co_return;
                    TRACE_PLOT("ws updates dropped", static_cast<int64_t>(item.error().skipped));
                    LOGF(WARNING, "websocket client lagged: dropped %u live item(s)", item.error().skipped);
                    continue;
                }
                // Spectrum rows are for the spectrum call.
                if (auto* frames = std::get_if<std::shared_ptr<const std::vector<api::Frame>>>(&*item)) {
                    for (const api::Frame& f : **frames) *update.add_frames() = f;
                }
                continue;
            }
            {
                TRACE_ZONE_VALUE("ws_join_update", update.frames_size());
                // Copies, so no watch lock is held past this statement.
                LiveAircraft now_aircraft = *aircraft.borrow_and_update();
                std::optional<api::InputStatus> now_status = *status.borrow_and_update();
                for (const auto& [icao, ac] : now_aircraft) {
                    auto it = sent_aircraft.find(icao);
                    if (it == sent_aircraft.end() || !MessageDifferencer::Equals(it->second, ac))
                        *update.add_aircraft() = ac;
                }
                if (now_status && (!sent_status || !MessageDifferencer::Equals(*sent_status, *now_status)))
                    *update.mutable_input_status() = *now_status;
                sent_aircraft = std::move(now_aircraft);
                sent_status = std::move(now_status);
            }
            if (has_anything(update)) co_yield std::exchange(update, api::Update{});
        }
    }

    // While this is in progress, the pipeline computes the spectrum rows it sends.
    coro::CoroStream<api::SpectrumRow> Handle(const api::SpectrumRequest&) {
        if (!m_settings.live) throw CallError(api::Error::NO_LIVE_INPUT, "the server has no live input");
        auto live = m_live.live.resubscribe();
        SpectrumViewer viewer(m_live.settings.clone());
        for (;;) {
            // As in the subscription, a slow client holds this at its co_yield until the ring overtakes it.
            auto item = co_await live.recv();
            if (!item) {
                if (item.error().kind != coro::BroadcastRecvError::Kind::Lagged) co_return;
                TRACE_PLOT("ws spectrum dropped", static_cast<int64_t>(item.error().skipped));
                LOGF(WARNING, "websocket spectrum client lagged: dropped %u live item(s)", item.error().skipped);
                continue;
            }
            if (auto* row = std::get_if<std::shared_ptr<const api::SpectrumRow>>(&*item)) co_yield **row;
        }
    }

    coro::Coro<api::Snapshot> Handle(const api::SnapshotRequest& request) {
        AircraftHistory db = history();
        co_return answered(co_await db.snapshot(request));
    }

    coro::Coro<api::AircraftHistory> Handle(const api::AircraftHistoryRequest& request) {
        AircraftHistory db = history();
        co_return answered(co_await db.aircraft(request));
    }

    coro::Coro<api::Activity> Handle(const api::ActivityRequest& request) {
        AircraftHistory db = history();
        co_return answered(co_await db.activity(request));
    }

    coro::Coro<api::Heatmap> Handle(const api::HeatmapRequest& request) {
        AircraftHistory db = history();
        if (!(request.east() > request.west()) || !(request.north() > request.south())) {  // NaN too
            throw CallError(api::Error::INVALID_ARGUMENT,
                            "the heatmap's area is empty: east must be over west, and north over south");
        }
        co_return answered(co_await db.heatmap(request));
    }

    // For the history calls: a clone of m_history, or CallError NO_HISTORY without one.
    AircraftHistory history() const {
        if (!m_history) throw CallError(api::Error::NO_HISTORY, "the server has no history database");
        return m_history->clone();
    }

    // A history query's reply, or CallError INTERNAL for a query that failed: the history task has logged why.
    template <typename Reply>
    static Reply answered(std::optional<Reply> reply) {
        if (!reply) throw CallError(api::Error::INTERNAL, "the history query failed");
        return std::move(*reply);
    }

    ApiReceivers m_live;
    std::optional<AircraftHistory> m_history;
    Settings m_settings;
    coro::MpscSender<std::string> m_out;
    std::optional<coro::MpscSender<uint32_t>> m_done_tx;  // optional only to be filled in the constructor
    std::optional<coro::MpscReceiver<uint32_t>> m_done_rx;
    uint32_t m_last_id = 0;
};

// Queues each binary message for the connection. Throws when the client disconnects.
coro::Coro<void> read_socket(coro::WsStream& ws, coro::MpscSender<std::vector<std::byte>> in) {
    for (;;) {
        coro::WsStream::Message msg = co_await ws.receive();
        if (msg.is_text) {
            LOGF(WARNING, "ignoring a websocket text message (%zu bytes): the API's messages are binary",
                 msg.data.size());
            continue;
        }
        if (!co_await in.send(std::move(msg.data))) co_return;
    }
}

// The connection's only writer: sends each message the calls queue, in the order they queued them. Throws
// when a send takes over kSendTimeout.
coro::Coro<void> write_socket(coro::WsStream& ws, coro::MpscReceiver<std::string> out) {
    while (auto msg = co_await out.recv()) {
        auto sent = co_await coro::timeout(kSendTimeout, ws.send(std::as_bytes(std::span(*msg))));
        if (sent.index() != 0) {
            throw std::runtime_error("a send took over " + std::to_string(kSendTimeout.count()) +
                                     " s, so the client is closed");
        }
    }
}

// `conn` is a parameter, not a local, since its calls are drained after this body finishes: a parameter is
// destroyed after that, and a local before. `out` is what `conn` sends on.
coro::Coro<void> handle_connection(coro::WsStream ws, ApiConnection conn, coro::MpscReceiver<std::string> out) {
    try {
        auto [in_tx, in_rx] = coro::mpsc_channel<std::vector<std::byte>>(kReadQueue);
        // Whichever throws first, on disconnect or a slow send, cancels the others.
        co_await coro::join(read_socket(ws, std::move(in_tx)),
                            conn.run(std::move(in_rx)),
                            write_socket(ws, std::move(out)));
    } catch (const std::exception& e) {
        LOGF(INFO, "websocket client ended: %s", e.what());
    }
}

coro::Coro<void> run(uint16_t port, ApiReceivers inputs, ApiConnection::Settings settings,
                     std::optional<AircraftHistory> history) {
    coro::WsListener listener = co_await coro::WsListener::bind("0.0.0.0", port);
    LOGF(INFO, "websocket listening on port %u: an update every %g s unless a subscription asks otherwise, %s, %s",
         port, std::chrono::duration<double>(settings.default_interval).count(),
         settings.live ? "spectrum while a client watches it" : "no spectrum",
         history ? "history answered" : "no history");
    // Runs until the task is cancelled: Ctrl+C is handled in main(), and ~Runtime cancels and drains this
    // task (see api_server_spawn()). JoinSet's destructor then cancels and drains any remaining clients.
    coro::JoinSet<void> clients;
    for (;;) {
        try {
            // when() skips the JoinSet until it has a client: next() on an empty one resolves at once.
            auto result = co_await coro::select(listener.accept(),
                                                coro::when(!clients.empty(), [&] { return coro::next(clients); }));
            if (result.index() == 0) {
                auto& ws = std::get<0>(result).value;
                std::optional<AircraftHistory> client_history;
                if (history) client_history = history->clone();
                ApiReceivers client_inputs{inputs.live.resubscribe(), inputs.aircraft.clone(),
                                           inputs.input_status.clone(), inputs.settings.clone()};
                auto [out_tx, out_rx] = coro::mpsc_channel<std::string>(kWriteQueue);
                clients
                    .build_task(handle_connection(
                        std::move(ws),
                        ApiConnection(std::move(client_inputs), std::move(client_history), settings,
                                      std::move(out_tx)),
                        std::move(out_rx)))
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

std::pair<ApiSenders, ApiReceivers> api_channels() {
    auto [live_tx, live_rx] = coro::broadcast_channel<LiveItem>(kLiveQueueItems);
    auto [aircraft_tx, aircraft_rx] = coro::watch_channel(LiveAircraft{});
    auto [status_tx, status_rx] = coro::watch_channel(std::optional<api::InputStatus>{});
    auto [settings_tx, settings_rx] = coro::watch_channel(PipelineSettings{});
    return {ApiSenders{std::move(live_tx), std::move(aircraft_tx), std::move(status_tx), std::move(settings_rx)},
            ApiReceivers{std::move(live_rx), std::move(aircraft_rx), std::move(status_rx), std::move(settings_tx)}};
}

void api_server_spawn(uint16_t port, std::chrono::nanoseconds default_interval, bool live, ApiReceivers inputs,
                      std::optional<AircraftHistory> history) {
    // Detached: run() accepts clients until the runtime's destructor cancels and drains it.
    coro::build_task()
        .name("ws_accept")
        .spawn(run(port, std::move(inputs), ApiConnection::Settings{default_interval, live}, std::move(history)))
        .detach();
}
