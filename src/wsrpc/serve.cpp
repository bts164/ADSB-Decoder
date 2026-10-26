#include "wsrpc/serve.h"

#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <coro/coro.h>
#include <coro/io/ws_stream.h>
#include <coro/stream.h>
#include <coro/sync/join.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/select.h>
#include <coro/sync/timeout.h>
#include <coro/sync/when.h>
#include <coro/task/cancel_handle.h>
#include <coro/task/join_set.h>
#include <coro/task/spawn_builder.h>

#include "common/log.h"
#include "common/trace.h"

namespace wsrpc {

namespace detail {

coro::Coro<bool> send(coro::MpscSender<std::string>& out, const v1::ServerFrame& frame) {
    VLOGF(2, "websocket server sent: %s", frame.ShortDebugString());
    std::string bytes;
    {
        TRACE_ZONE("ws_serialize");
        bytes = frame.SerializeAsString();
    }
    co_return (co_await out.send(std::move(bytes))).has_value();
}

// A streaming call is something a client turned on, such as a watch; a unary call is one of many, such as each
// seek of a history scrubber.
void log_started(const Call& call, bool notable, const std::string& request) {
    if (notable)
        LOGF(INFO, "websocket call %u: %s started%s%s", call.id, call.method, request.empty() ? "" : ": ", request);
    else
        VLOGF(1, "websocket call %u: %s started%s%s", call.id, call.method, request.empty() ? "" : ": ", request);
}

}  // namespace detail

namespace {

v1::ServerFrame error_frame(uint32_t id, Code code, std::string message) {
    v1::ServerFrame frame;
    frame.set_id(id);
    frame.set_end(true);
    v1::Error& error = *frame.mutable_error();
    error.set_code(code);
    error.set_message(std::move(message));
    return frame;
}

// A CallError keeps its code; anything else is a bug in the handler, so it's logged and INTERNAL.
void set_error(v1::ServerFrame& frame, std::exception_ptr e) {
    frame.clear_reply();
    v1::Error& error = *frame.mutable_error();
    try {
        std::rethrow_exception(e);
    } catch (const CallError& c) {
        error.set_code(c.code());
        error.set_message(c.what());
    } catch (const std::exception& x) {
        LOGF(ERROR, "websocket call %u failed: %s", frame.id(), x.what());
        error.set_code(v1::Error::INTERNAL);
        error.set_message(x.what());
    } catch (...) {
        LOGF(ERROR, "websocket call %u failed with an unknown exception", frame.id());
        error.set_code(v1::Error::INTERNAL);
        error.set_message("unknown exception");
    }
}

// A message as a ClientFrame, or nothing for one that isn't, which is logged and dropped.
std::optional<v1::ClientFrame> parse_frame(const coro::WsStream::Message& message) {
    if (message.is_text) {
        LOGF(WARNING, "ignoring a websocket text message (%zu bytes): wsrpc's frames are binary", message.data.size());
        return std::nullopt;
    }
    v1::ClientFrame frame;
    if (!frame.ParseFromArray(message.data.data(), static_cast<int>(message.data.size()))) {
        LOGF(WARNING, "ignoring a websocket client message that isn't a ClientFrame (%zu bytes)",
             message.data.size());
        return std::nullopt;
    }
    VLOGF(2, "websocket client sent: %s", frame.ShortDebugString());
    return frame;
}

/**
 * The connection's only reader: answers the hello, starts a task for each call and acts on its later frames, and
 * sends each call's `end` when its task finishes, until the client disconnects, which throws. Calls still in
 * progress then are cancelled.
 *
 * Each call's task is tagged with its id. The task sends its call's other frames on `out` and returns its last
 * reply, which this sends with the `end`, or an Error if the task failed or was cancelled. So every call gets
 * exactly one `end`, after the rest of its frames.
 */
coro::Coro<void> read_socket(coro::WsStream& ws, const std::vector<Service*>& services, const Limits& limits,
                             coro::MpscSender<std::string> out) {
    {
        // A frame before the hello closes the connection.
        std::optional<v1::ClientFrame> first;
        while (!first) first = parse_frame(co_await ws.receive());
        if (!first->has_hello()) throw std::runtime_error("the client's first frame isn't a hello");
        v1::ServerFrame answer;
        v1::Hello& hello = *answer.mutable_hello();
        for (const Service* s : services) hello.add_services(std::string(s->name()));
        for (const std::string& known : first->hello().services()) {
            bool served = false;
            for (const Service* s : services) served = served || s->name() == known;
            if (!served) LOGF(INFO, "websocket client knows %s, which this server doesn't serve", known);
        }
        VLOGF(1, "websocket client hello: knows %s", first->hello().ShortDebugString());
        co_await detail::send(out, answer);
    }

    struct Running {
        coro::CancelHandle task;
        // A client stream's input, until the client's `end` or a full queue closes it; unset for other calls.
        std::optional<coro::MpscSender<v1::ClientFrame>> in;
        std::string method;
        bool notable = false;  // logged at INFO, not VLOGF(1)
    };
    // Locals, so that however this ends, its frame is destroyed and the calls' tasks are cancelled and drained
    // before serve() returns.
    coro::JoinSet<std::optional<std::string>, uint32_t> tasks;  // each call's last reply, tagged with its id
    std::unordered_map<uint32_t, Running> active;               // each call until this has its outcome
    uint32_t last_id = 0;
    for (;;) {
        // Dropping a pending receive() loses nothing: a message that arrives meanwhile waits for the next.
        auto woke = co_await coro::select(ws.receive(), coro::when(!tasks.empty(), [&] { return coro::next(tasks); }));
        if (woke.index() == 1) {
            // when() skips an empty set, so there's an outcome.
            coro::JoinResult<std::optional<std::string>, uint32_t> done = std::move(*std::get<1>(woke).value);
            const uint32_t id = done.tag();
            auto node = active.extract(id);
            const Running& call = node.mapped();
            v1::ServerFrame last;
            last.set_id(id);
            last.set_end(true);
            if (done.has_value()) {
                if (std::optional<std::string> reply = std::move(done).value()) last.set_reply(std::move(*reply));
            } else if (std::exception_ptr e = done.exception()) {
                set_error(last, e);
            } else {
                last = error_frame(id, v1::Error::CANCELLED, "cancelled by the client");
            }
            const char* how = done.is_cancelled() ? " cancelled" : last.has_error() ? " with an error" : "";
            if (call.notable)
                LOGF(INFO, "websocket call %u: %s ended%s", id, call.method, how);
            else
                VLOGF(1, "websocket call %u: %s ended%s", id, call.method, how);
            co_await detail::send(out, last);
            continue;
        }

        std::optional<v1::ClientFrame> parsed = parse_frame(std::get<0>(woke).value);
        if (!parsed) continue;
        v1::ClientFrame& frame = *parsed;
        const uint32_t id = frame.id();
        const bool end = frame.end();

        if (frame.has_hello()) {
            LOGF(WARNING, "ignoring a websocket client's second hello");
            continue;
        }

        // A cancel's id is the call to cancel. One not in progress is ignored: its `end` is already queued.
        if (frame.cancel()) {
            if (auto it = active.find(id); it != active.end()) it->second.task.cancel();
            continue;
        }

        // A later frame of a client-streaming call: the call's own task parses its request.
        if (auto it = active.find(id); it != active.end()) {
            Running& call = it->second;
            if (!call.in) {
                LOGF(WARNING, "ignoring a websocket client frame for call %u, which takes no more input", id);
                continue;
            }
            // Not send(): waiting for room would hold back every call on the connection, including a cancel of
            // this one.
            auto sent = call.in->try_send(std::move(frame));
            if (!sent && sent.error().kind == coro::TrySendError<v1::ClientFrame>::Kind::Full) {
                TRACE_PLOT("ws call input dropped", int64_t{1});
                LOGF(WARNING, "websocket call %u: the client sent more than %zu requests ahead of it, so it fails "
                     "once it has handled those",
                     id, limits.call_input_queue);
            }
            // Closing the queue ends the call's input once it has taken what's queued.
            if (!sent || end) call.in.reset();
            continue;
        }

        // A frame without a method, for an id already used, is a late one for a call that has ended, or was
        // refused: a reply would be a second `end` for it.
        if (frame.method().empty() && id <= last_id) {
            VLOGF(1, "ignoring a websocket client frame for call %u, which has ended", id);
            continue;
        }

        // A new call: start the method's adapter in a task of its own, tagged with its id. Returns the frame
        // refusing the call, if it's refused.
        auto start = [&]() -> std::optional<v1::ServerFrame> {
            if (id <= last_id) {
                return error_frame(id, v1::Error::INVALID_ARGUMENT,
                                   "call id " + std::to_string(id) + " isn't larger than the last, " +
                                       std::to_string(last_id));
            }
            last_id = id;
            std::string method = std::move(*frame.mutable_method());
            // "/package.Service/Method"
            const size_t slash = method.find('/', 1);
            if (method.size() < 2 || method[0] != '/' || slash == std::string::npos)
                return error_frame(id, v1::Error::UNIMPLEMENTED, "no such method: \"" + method + "\"");
            const std::string_view service_name = std::string_view(method).substr(1, slash - 1);
            Service* service = nullptr;
            for (Service* s : services) {
                if (s->name() == service_name) service = s;
            }
            if (!service) {
                return error_frame(id, v1::Error::UNIMPLEMENTED,
                                   "this server doesn't serve " + std::string(service_name));
            }
            const Method* m = service->find(std::string_view(method).substr(slash + 1));
            if (!m) return error_frame(id, v1::Error::UNIMPLEMENTED, "no such method: " + method);
            if (active.size() >= limits.client_calls) {
                return error_frame(id, v1::Error::RESOURCE_EXHAUSTED,
                                   std::to_string(limits.client_calls) + " calls are already in progress");
            }

            Running running{
                .task = {}, .in = std::nullopt, .method = method, .notable = m->kind != Method::Kind::Unary};
            Call call{.id = id,
                      .method = std::move(method),
                      .first = {},
                      .in = std::nullopt,
                      .input_queue = limits.call_input_queue,
                      .out = out.clone()};
            if (m->kind == Method::Kind::ClientStreaming || m->kind == Method::Kind::Bidi) {
                auto [in_tx, in_rx] = coro::mpsc_channel<v1::ClientFrame>(limits.call_input_queue);
                call.in = std::move(in_rx);
                if (!end) running.in = std::move(in_tx);
            }
            call.first = std::move(frame);
            running.task = tasks.build_task(id, m->run(*service, std::move(call))).name("ws_call").spawn();
            active.emplace(id, std::move(running));
            return std::nullopt;
        };
        // A refusal waits here for room in `out`, holding the reader back; only a client that sends bad calls
        // faster than it reads replies is held back by it.
        if (std::optional<v1::ServerFrame> refused = start()) {
            VLOGF(1, "websocket call %u refused: %s", id, refused->error().message());
            co_await detail::send(out, *refused);
        }
    }
}

// The connection's only writer: sends each frame the calls queue, in the order they queued them. Throws when a
// send takes over `send_timeout`.
coro::Coro<void> write_socket(coro::WsStream& ws, coro::MpscReceiver<std::string> out,
                              std::chrono::seconds send_timeout) {
    while (auto bytes = co_await out.recv()) {
        auto sent = co_await coro::timeout(send_timeout, ws.send(std::as_bytes(std::span(*bytes))));
        if (sent.index() != 0) {
            throw std::runtime_error("a send took over " + std::to_string(send_timeout.count()) +
                                     " s, so the client is closed");
        }
    }
}

}  // namespace

coro::Coro<void> serve(coro::WsStream ws, std::vector<Service*> services, Limits limits) {
    auto [out_tx, out_rx] = coro::mpsc_channel<std::string>(limits.write_queue);
    try {
        // Whichever throws first, on disconnect or a slow send, cancels the other.
        co_await coro::join(read_socket(ws, services, limits, std::move(out_tx)),
                            write_socket(ws, std::move(out_rx), limits.send_timeout));
    } catch (const std::exception& e) {
        LOGF(INFO, "websocket client ended: %s", e.what());
    }
}

}  // namespace wsrpc
