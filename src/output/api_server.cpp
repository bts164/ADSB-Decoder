#include "output/api_server.h"

#include <cstddef>
#include <exception>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/defer.h>
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
#include <coro/task/cancel_handle.h>
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

// A client-streaming call's messages waiting for its handler. One more fails the call, once it has handled these.
constexpr size_t kCallInputQueue = 16;

// Serialized messages waiting for the connection's writer, from all its calls.
constexpr size_t kWriteQueue = 16;

// Capture calls on their way to the pipeline, from all the connections.
constexpr size_t kCaptureQueue = 4;

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
void put(api::ServerMessage& m, api::CaptureResult r) { *m.mutable_capture() = std::move(r); }

// Every call a client can make: its case in ClientMessage's oneof, its field, and its request type. The switch in
// ApiConnection::read_socket() has a case for each and no default, so -Wswitch flags a call missing here.
#define ADSB_API_CALLS(CALL)                                       \
    CALL(kServerInfo, server_info, ServerInfoRequest)              \
    CALL(kSubscribe, subscribe, SubscribeRequest)                  \
    CALL(kSpectrum, spectrum, SpectrumRequest)                     \
    CALL(kSnapshot, snapshot, SnapshotRequest)                     \
    CALL(kAircraftHistory, aircraft_history, AircraftHistoryRequest) \
    CALL(kActivity, activity, ActivityRequest)                     \
    CALL(kHeatmap, heatmap, HeatmapRequest)                        \
    CALL(kCapture, capture, CaptureRequest)

/**
 * The API's calls: a Handle() overload for each request type in ClientMessage, which answers the call. To add a
 * call, add its request to ClientMessage in api.proto, its Handle here, and its line to ADSB_API_CALLS below;
 * ApiConnection runs it.
 *
 * A Handle takes the call's request, or a coro::CoroStream of them for a call the client streams, and returns a
 * coro::Coro of its reply, or a coro::CoroStream of replies for a call the server streams. Its signature is all
 * ApiConnection needs to run the call. A handler fails by throwing a CallError. Since the calls run at once, a
 * handler takes its own clone of anything that is for one task at a time.
 */
class ApiHandlers {
public:
    struct Settings {
        std::chrono::nanoseconds default_interval;  // --ws-interval
        bool live;                                  // whether a pipeline publishes on the channels
        bool capture;                               // whether the pipeline takes capture calls
    };

    // Shortest time between a subscription's updates; a request for less gets this.
    static constexpr double kMinUpdateIntervalS = 0.1;
    // Longest time between a subscription's updates; a request for more is refused.
    static constexpr double kMaxUpdateIntervalS = 3600;

    ApiHandlers(ApiReceivers live, std::optional<AircraftHistory> history, Settings settings)
        : m_live(std::move(live)), m_history(std::move(history)), m_settings(settings) {}

    coro::Coro<api::ServerInfo> Handle(const api::ServerInfoRequest&) {
        api::ServerInfo info;
        info.set_history(m_history.has_value());
        info.set_spectrum(m_settings.live);
        info.set_default_interval_s(std::chrono::duration<double>(m_settings.default_interval).count());
        info.set_capture(m_settings.capture);
        info.set_capture_max_s(m_settings.capture ? CaptureCall::kMaxDurationS : 0);
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
            if (update.frames_size() > 0 || update.aircraft_size() > 0 || update.has_input_status())
                co_yield std::exchange(update, api::Update{});
        }
    }

    // While this is in progress, the pipeline computes the spectrum rows it sends.
    coro::CoroStream<api::SpectrumRow> Handle(const api::SpectrumRequest&) {
        if (!m_settings.live) throw CallError(api::Error::NO_LIVE_INPUT, "the server has no live input");
        auto live = m_live.live.resubscribe();
        coro::WatchSender<PipelineSettings> settings = m_live.settings.clone();
        settings.send_if_modified([](PipelineSettings& s) {
            ++s.spectrum_viewers;
            return true;
        });
        coro::Defer uncount([&settings] {
            settings.send_if_modified([](PipelineSettings& s) {
                --s.spectrum_viewers;
                return true;
            });
        });
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

    // Answered by the pipeline's capture task for the point, once the capture is done; a cancel drops the reply's
    // receiver, which ends the capture.
    coro::Coro<api::CaptureResult> Handle(const api::CaptureRequest& request) {
        if (!m_settings.capture) throw CallError(api::Error::NO_CAPTURE, "the server has no --capture-dir");
        if (request.point() != api::CaptureRequest::INPUT && request.point() != api::CaptureRequest::ENVELOPE)
            throw CallError(api::Error::INVALID_ARGUMENT, "no such capture point");
        const double s = request.duration_s();
        if (!(s > 0 && s <= CaptureCall::kMaxDurationS)) {  // NaN too
            throw CallError(api::Error::INVALID_ARGUMENT, "duration_s " + std::to_string(s) +
                                                              " isn't over 0 and at most " +
                                                              std::to_string(CaptureCall::kMaxDurationS));
        }
        const std::string& name = request.name();
        if (name.empty() || name.front() == '.' || name.find_first_of(std::string_view("/\0", 2)) != name.npos)
            throw CallError(api::Error::INVALID_ARGUMENT, "name must be a file name, not starting with '.'");
        auto [reply_tx, reply_rx] = coro::oneshot_channel<CaptureCall::Reply>();
        coro::MpscSender<CaptureCall> captures = m_live.captures.clone();
        if (!co_await captures.send(CaptureCall{request, std::move(reply_tx)}))
            throw CallError(api::Error::INTERNAL, "the pipeline has stopped taking captures");
        auto reply = co_await reply_rx.recv();
        if (!reply) throw CallError(api::Error::INTERNAL, "the capture task ended without replying");
        if (!*reply) throw CallError(reply->error().code(), reply->error().message());
        co_return std::move(**reply);
    }

    coro::Coro<api::Snapshot> Handle(const api::SnapshotRequest& request) {
        if (!m_history) throw CallError(api::Error::NO_HISTORY, "the server has no history database");
        AircraftHistory db = m_history->clone();
        auto reply = co_await db.snapshot(request);
        if (!reply) throw CallError(api::Error::INTERNAL, "the history query failed");  // the history task logged why
        co_return std::move(*reply);
    }

    coro::Coro<api::AircraftHistory> Handle(const api::AircraftHistoryRequest& request) {
        if (!m_history) throw CallError(api::Error::NO_HISTORY, "the server has no history database");
        AircraftHistory db = m_history->clone();
        auto reply = co_await db.aircraft(request);
        if (!reply) throw CallError(api::Error::INTERNAL, "the history query failed");  // the history task logged why
        co_return std::move(*reply);
    }

    coro::Coro<api::Activity> Handle(const api::ActivityRequest& request) {
        if (!m_history) throw CallError(api::Error::NO_HISTORY, "the server has no history database");
        AircraftHistory db = m_history->clone();
        auto reply = co_await db.activity(request);
        if (!reply) throw CallError(api::Error::INTERNAL, "the history query failed");  // the history task logged why
        co_return std::move(*reply);
    }

    coro::Coro<api::Heatmap> Handle(const api::HeatmapRequest& request) {
        if (!m_history) throw CallError(api::Error::NO_HISTORY, "the server has no history database");
        AircraftHistory db = m_history->clone();
        if (!(request.east() > request.west()) || !(request.north() > request.south())) {  // NaN too
            throw CallError(api::Error::INVALID_ARGUMENT,
                            "the heatmap's area is empty: east must be over west, and north over south");
        }
        auto reply = co_await db.heatmap(request);
        if (!reply) throw CallError(api::Error::INTERNAL, "the history query failed");  // the history task logged why
        co_return std::move(*reply);
    }

private:
    ApiReceivers m_live;
    std::optional<AircraftHistory> m_history;
    Settings m_settings;
};

/**
 * One client's connection: reads its ClientMessages, runs each call's Handle in a task of its own, and writes what
 * the calls send.
 *
 * Each call's task is tagged with its id. The task sends the call's other messages on `m_out` and returns its last;
 * read_socket() sends that as the call's `end`, or an Error if the task failed or was cancelled. So every call gets
 * exactly one `end`, after the rest of its messages.
 *
 * The calls' tasks use this object, so it must outlive read_socket(): see serve().
 */
class ApiConnection {
public:
    // Most calls one connection may have in progress at once.
    static constexpr size_t kClientCalls = 8;

    ApiConnection(ApiHandlers handlers, coro::MpscSender<std::string> out)
        : m_handlers(std::move(handlers)), m_out(std::move(out)) {}

    // Serves the client on `ws` until it disconnects or a send times out. `conn` is a parameter, not a local, since
    // its calls are drained after this body finishes: a parameter is destroyed after that, and a local before.
    // `out` is what `conn` sends on.
    static coro::Coro<void> serve(coro::WsStream ws, ApiConnection conn, coro::MpscReceiver<std::string> out) {
        try {
            // Whichever throws first, on disconnect or a slow send, cancels the other.
            co_await coro::join(conn.read_socket(ws), write_socket(ws, std::move(out)));
        } catch (const std::exception& e) {
            LOGF(INFO, "websocket client ended: %s", e.what());
        }
    }

private:
    // A client stream's requests, for its handler: `first`, from the message that started the call, then each
    // later message's, up to the client's `end`. Input that ends without one was cut off: read_socket() closed the
    // call's queue when it was full.
    template<typename Request>
    static coro::CoroStream<Request> client_requests(Request first, bool end,
                                                     coro::MpscReceiver<api::ClientMessage> in) {
        co_yield std::move(first);
        while (!end) {
            auto msg = co_await in.recv();
            if (!msg) {
                throw CallError(api::Error::RESOURCE_EXHAUSTED,
                                "the client sent more than " + std::to_string(kCallInputQueue) +
                                    " messages ahead of the call");
            }
            end = msg->end();
            if (end && msg->call_case() == api::ClientMessage::CALL_NOT_SET) co_return;
            Request* request = nullptr;
#define ADSB_REQUEST_OF(CASE, FIELD, TYPE)                                                           \
    if constexpr (std::is_same_v<Request, api::TYPE>) {                                              \
        if (msg->call_case() == api::ClientMessage::CASE) request = msg->mutable_##FIELD();          \
    }
            ADSB_API_CALLS(ADSB_REQUEST_OF)
#undef ADSB_REQUEST_OF
            if (!request) throw CallError(api::Error::INVALID_ARGUMENT, "a message for another kind of call");
            co_yield std::move(*request);
        }
    }

    /**
     * The connection's only reader: acts on each ClientMessage the client sends on `ws`, and sends each call's
     * `end` when its task finishes, until the client disconnects, which throws. Calls still in progress then are
     * cancelled.
     */
    coro::Coro<void> read_socket(coro::WsStream& ws) {
        struct Call {
            coro::CancelHandle task;
            // A client stream's input, until the client's `end` or a full queue closes it; unset for other calls.
            std::optional<coro::MpscSender<api::ClientMessage>> in;
            bool notable = false;  // logged at INFO, not VLOGF(1)
        };
        // A local, not a member: read_socket()'s frame is destroyed however it ends, dropping each call's task,
        // which cancels it, while this object, which the tasks use, still exists.
        coro::JoinSet<api::ServerMessage, uint32_t> tasks;  // each tagged with its call's id
        std::unordered_map<uint32_t, Call> active;           // each call until read_socket() has its outcome
        for (;;) {
            // Dropping a pending receive() loses nothing: a message that arrives meanwhile waits for the next.
            auto woke = co_await coro::select(
                ws.receive(), coro::when(!tasks.empty(), [&] { return coro::next(tasks); }));
            if (woke.index() == 1) {
                // when() skips an empty set, so there's an outcome.
                coro::JoinResult<api::ServerMessage, uint32_t> done = std::move(*std::get<1>(woke).value);
                const uint32_t id = done.tag();
                const bool notable = active.at(id).notable;
                active.erase(id);
                api::ServerMessage last;
                if (done.has_value()) last = std::move(done).value();
                last.set_id(id);
                last.set_end(true);
                if (std::exception_ptr e = done.exception()) set_error(last, e);
                if (done.is_cancelled()) last = error_message(id, api::Error::CANCELLED, "cancelled by the client");
                const char* how = done.is_cancelled() ? " cancelled" : last.has_error() ? " with an error" : "";
                if (notable)
                    LOGF(INFO, "websocket call %u: ended%s", id, how);
                else
                    VLOGF(1, "websocket call %u: ended%s", id, how);
                co_await send(m_out, last);
                continue;
            }

            coro::WsStream::Message& frame = std::get<0>(woke).value;
            if (frame.is_text) {
                LOGF(WARNING, "ignoring a websocket text message (%zu bytes): the API's messages are binary",
                     frame.data.size());
                continue;
            }
            api::ClientMessage msg;
            if (!msg.ParseFromArray(frame.data.data(), static_cast<int>(frame.data.size()))) {
                LOGF(WARNING, "ignoring a websocket client message that isn't a ClientMessage (%zu bytes)",
                     frame.data.size());
                continue;
            }
            VLOGF(2, "websocket client sent: %s", msg.ShortDebugString());
            const uint32_t id = msg.id();
            const bool end = msg.end();

            // A cancel's id is the call to cancel. One not in progress is ignored: its `end` is already queued.
            if (msg.call_case() == api::ClientMessage::kCancel) {
                if (auto it = active.find(id); it != active.end()) it->second.task.cancel();
                continue;
            }

            // A later message of a client-streaming call: the call's own task unwraps it.
            if (auto it = active.find(id); it != active.end()) {
                Call& call = it->second;
                if (!call.in) {
                    LOGF(WARNING, "ignoring a websocket client message for call %u, which takes no more input", id);
                    continue;
                }
                // Not send(): waiting for room would hold back every call on the connection, including a cancel
                // of this one.
                auto sent = call.in->try_send(std::move(msg));
                if (!sent && sent.error().kind == coro::TrySendError<api::ClientMessage>::Kind::Full) {
                    TRACE_PLOT("ws call input dropped", int64_t{1});
                    LOGF(WARNING, "websocket call %u: the client sent more than %zu messages ahead of it, so it fails "
                         "once it has handled those",
                         id, kCallInputQueue);
                }
                // Closing the queue ends the call's input once it has taken what's queued.
                if (!sent || end) call.in.reset();
                continue;
            }

            // A new call: start it in a task of its own, tagged with its id. One whose Handle takes a stream of
            // requests gets them through a queue; any other gets its request. Returns the reply refusing the call,
            // if it's refused.
            auto start = [&]<typename Request>(Request request) -> std::optional<api::ServerMessage> {
                if (id <= m_last_id) {
                    return error_message(id, api::Error::INVALID_ARGUMENT,
                                         "call id " + std::to_string(id) + " isn't larger than the last, " +
                                             std::to_string(m_last_id));
                }
                m_last_id = id;
                if (active.size() >= kClientCalls) {
                    return error_message(id, api::Error::RESOURCE_EXHAUSTED,
                                         std::to_string(kClientCalls) + " calls are already in progress");
                }
                const std::string args = request.ShortDebugString();
                // A streaming call is something a client turned on, such as the spectrum, as is a capture; a
                // unary call is one of many, such as each seek of the history scrubber.
                Call call{.task = {},
                          .in = std::nullopt,
                          .notable = kTakesStream<Request> || kReturnsStream<Request> ||
                                     std::is_same_v<Request, api::CaptureRequest>};
                if constexpr (kTakesStream<Request>) {
                    auto [in_tx, in_rx] = coro::mpsc_channel<api::ClientMessage>(kCallInputQueue);
                    if (!end) call.in = std::move(in_tx);
                    call.task = tasks
                                    .build_task(id, run_call(id, client_requests(std::move(request), end,
                                                                                 std::move(in_rx))))
                                    .name("ws_call")
                                    .spawn();
                } else {
                    call.task = tasks.build_task(id, run_call(id, std::move(request))).name("ws_call").spawn();
                }
                const auto name = Request::descriptor()->name();
                if (call.notable)
                    LOGF(INFO, "websocket call %u: %s started%s%s", id, name, args.empty() ? "" : ": ", args);
                else
                    VLOGF(1, "websocket call %u: %s started%s%s", id, name, args.empty() ? "" : ": ", args);
                active.emplace(id, std::move(call));
                return std::nullopt;
            };
            std::optional<api::ServerMessage> refused;
            // No default: a call added to the .proto and missing from ADSB_API_CALLS is a -Wswitch warning.
            switch (msg.call_case()) {
#define ADSB_START_CALL(CASE, FIELD, TYPE) \
    case api::ClientMessage::CASE: refused = start(std::move(*msg.mutable_##FIELD())); break;
                ADSB_API_CALLS(ADSB_START_CALL)
#undef ADSB_START_CALL
            case api::ClientMessage::kCancel: break;  // handled above
            case api::ClientMessage::CALL_NOT_SET:    // a call newer than this server, or none at all
                refused = error_message(id, api::Error::UNIMPLEMENTED, "unknown call");
                break;
            }
            // A refusal waits here for room in `m_out`, holding the reader back; only a client that sends bad
            // calls faster than it reads replies is held back by it.
            if (refused) co_await send(m_out, *refused);
        }
    }

    // The connection's only writer: sends each message the calls queue, in the order they queued them. Throws
    // when a send takes over kSendTimeout.
    static coro::Coro<void> write_socket(coro::WsStream& ws, coro::MpscReceiver<std::string> out) {
        while (auto msg = co_await out.recv()) {
            auto sent = co_await coro::timeout(kSendTimeout, ws.send(std::as_bytes(std::span(*msg))));
            if (sent.index() != 0) {
                throw std::runtime_error("a send took over " + std::to_string(kSendTimeout.count()) +
                                         " s, so the client is closed");
            }
        }
    }

    // A call's kind comes from its Handle: whether it takes a stream of requests, and whether it returns a
    // coro::Stream.
    template<typename Request>
    static constexpr bool kTakesRequest = requires(ApiHandlers& h, const Request& r) { h.Handle(r); };
    template<typename Request>
    static constexpr bool kTakesStream =
        requires(ApiHandlers& h, coro::CoroStream<Request> in) { h.Handle(std::move(in)); };
    template<typename Request>
    static constexpr bool kReturnsStream =
        requires(ApiHandlers& h, const Request& r) {
            { h.Handle(r) } -> coro::Stream;
        } || requires(ApiHandlers& h, coro::CoroStream<Request> in) {
            { h.Handle(std::move(in)) } -> coro::Stream;
        };

    // The run_call() overloads, one per kind of call, each return the call's last message for read_socket() to send
    // as its `end`. The request or requests live in the frame, so the handler can keep a reference to them.

    template<typename Request>
        requires(kTakesRequest<Request> && !kReturnsStream<Request>)
    coro::Coro<api::ServerMessage> run_call(uint32_t, Request request) {
        api::ServerMessage reply;
        put(reply, co_await m_handlers.Handle(request));
        co_return reply;
    }

    template<typename Request>
        requires(kTakesRequest<Request> && kReturnsStream<Request>)
    coro::Coro<api::ServerMessage> run_call(uint32_t id, Request request) {
        co_return co_await send_items(id, m_handlers.Handle(request));
    }

    template<typename Request>
        requires(kTakesStream<Request> && !kReturnsStream<Request>)
    coro::Coro<api::ServerMessage> run_call(uint32_t, coro::CoroStream<Request> requests) {
        api::ServerMessage reply;
        put(reply, co_await m_handlers.Handle(std::move(requests)));
        co_return reply;
    }

    template<typename Request>
        requires(kTakesStream<Request> && kReturnsStream<Request>)
    coro::Coro<api::ServerMessage> run_call(uint32_t id, coro::CoroStream<Request> requests) {
        co_return co_await send_items(id, m_handlers.Handle(std::move(requests)));
    }

    // A streaming handler's items, each sent before it's asked for the next, so a client that reads slowly holds
    // the handler back at its co_yield. Returns the stream's `end`, which has no body.
    template<coro::Stream Items>
    coro::Coro<api::ServerMessage> send_items(uint32_t id, Items items) {
        coro::MpscSender<std::string> out = m_out.clone();
        while (auto item = co_await coro::next(items)) {
            api::ServerMessage msg;
            msg.set_id(id);
            put(msg, std::move(*item));
            if (!co_await send(out, msg)) break;
        }
        co_return api::ServerMessage{};
    }

    ApiHandlers m_handlers;
    coro::MpscSender<std::string> m_out;
    uint32_t m_last_id = 0;
};

#undef ADSB_API_CALLS

coro::Coro<void> run(uint16_t port, ApiReceivers inputs, ApiHandlers::Settings settings,
                     std::optional<AircraftHistory> history) {
    coro::WsListener listener = co_await coro::WsListener::bind("0.0.0.0", port);
    LOGF(INFO, "websocket listening on port %u: an update every %g s unless a subscription asks otherwise, %s, %s, "
         "%s",
         port, std::chrono::duration<double>(settings.default_interval).count(),
         settings.live ? "spectrum while a client watches it" : "no spectrum",
         settings.capture ? "captures taken" : "no captures", history ? "history answered" : "no history");
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
                                           inputs.input_status.clone(), inputs.settings.clone(),
                                           inputs.captures.clone()};
                auto [out_tx, out_rx] = coro::mpsc_channel<std::string>(kWriteQueue);
                ApiConnection conn(ApiHandlers(std::move(client_inputs), std::move(client_history), settings),
                                   std::move(out_tx));
                clients.build_task(ApiConnection::serve(std::move(ws), std::move(conn), std::move(out_rx)))
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
    auto [captures_tx, captures_rx] = coro::mpsc_channel<CaptureCall>(kCaptureQueue);
    return {ApiSenders{std::move(live_tx), std::move(aircraft_tx), std::move(status_tx), std::move(settings_rx),
                       std::move(captures_rx)},
            ApiReceivers{std::move(live_rx), std::move(aircraft_rx), std::move(status_rx), std::move(settings_tx),
                         std::move(captures_tx)}};
}

void api_server_spawn(uint16_t port, std::chrono::nanoseconds default_interval, bool live, bool capture,
                      ApiReceivers inputs, std::optional<AircraftHistory> history) {
    // Detached: run() accepts clients until the runtime's destructor cancels and drains it.
    coro::build_task()
        .name("ws_accept")
        .spawn(run(port, std::move(inputs), ApiHandlers::Settings{default_interval, live, capture},
                   std::move(history)))
        .detach();
}
