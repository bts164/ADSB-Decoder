#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/stream.h>
#include <coro/sync/mpsc.h>

#include "wsrpc/v1/wsrpc.pb.h"

/**
 * @file
 * What the code protoc-gen-wsrpc generates builds on: the base of each service's class, and the adapters its
 * method table names, which run a call of each kind on the service's handler. serve.h serves a connection.
 * doc/design/ui_api.md describes the protocol and the design.
 */

namespace wsrpc {

using Code = v1::Error::Code;

/** A call's failure, thrown by a handler and sent to the client as an Error with this code. */
class CallError : public std::runtime_error {
public:
    CallError(Code code, const std::string& message) : std::runtime_error(message), m_code(code) {}
    Code code() const { return m_code; }

private:
    Code m_code;
};

/**
 * A call as its adapter gets it from the connection's reader. `first` is the frame that started it; a call that
 * streams from the client gets its later frames on `in`, until one with `end`. Input that closes without one was
 * cut off: the reader closed the queue when it was full.
 */
struct Call {
    uint32_t id;
    std::string method;  // the path, e.g. "/adsb.api.v1.Receiver/WatchFrames", for the log
    v1::ClientFrame first;
    std::optional<coro::MpscReceiver<v1::ClientFrame>> in;
    size_t input_queue;  // the size of `in`, for the error when it overflows
    coro::MpscSender<std::string> out;  // the connection's writer, for a stream's replies
};

class Service;

/**
 * One method of a service: its name, its kind, and the adapter that runs a call of it. `run` returns the call's
 * last reply, serialized, for the reader to send with its `end`, or nothing for a stream's `end`.
 */
struct Method {
    enum class Kind { Unary, ServerStreaming, ClientStreaming, Bidi };

    std::string_view name;
    Kind kind;
    coro::Coro<std::optional<std::string>> (*run)(Service& service, Call call);
};

/** A service's methods, looked up by name. */
class MethodTable {
public:
    MethodTable(std::initializer_list<Method> methods) : m_methods(methods) {}

    const Method* find(std::string_view name) const {
        for (const Method& m : m_methods) {
            if (m.name == name) return &m;
        }
        return nullptr;
    }

private:
    std::vector<Method> m_methods;
};

/**
 * What the runtime sees of a service: its full name and its methods. Each generated class derives from this,
 * and a server's handlers derive from that class.
 */
class Service {
public:
    virtual ~Service() = default;
    virtual std::string_view name() const = 0;
    virtual const Method* find(std::string_view method) const = 0;
};

namespace detail {

// Defined in serve.cpp, so the adapters' instances share them.
coro::Coro<bool> send(coro::MpscSender<std::string>& out, const v1::ServerFrame& frame);
void log_started(const Call& call, bool notable, const std::string& request);

// A handler's class and its parameter and return types, from its member pointer.
template<typename T>
struct HandlerOf;
template<typename C, typename R, typename A>
struct HandlerOf<R (C::*)(A)> {
    using Class = C;
    using Result = R;
    using Param = std::remove_cvref_t<A>;
};

template<typename T>
struct IsCoro : std::false_type {};
template<typename T>
struct IsCoro<coro::Coro<T>> : std::true_type {
    using Value = T;
};

template<typename T>
struct IsCoroStream : std::false_type {};
template<typename T>
struct IsCoroStream<coro::CoroStream<T>> : std::true_type {
    using Value = T;
};

template<typename Request>
Request parse(const std::string& bytes) {
    Request request;
    if (!request.ParseFromString(bytes)) {
        throw CallError(v1::Error::INVALID_ARGUMENT,
                        "a request that doesn't parse as " + std::string(Request::descriptor()->full_name()));
    }
    return request;
}

// The request of a call that takes one.
template<typename Request>
Request single_request(const Call& call) {
    if (!call.first.has_request()) throw CallError(v1::Error::INVALID_ARGUMENT, "a call without its request");
    return parse<Request>(call.first.request());
}

// A client stream's requests, for its handler: the first frame's, if it has one, then each later frame's, up to
// the client's `end`. Takes a Call's input, so the adapter keeps the rest of it.
template<typename Request>
coro::CoroStream<Request> client_requests(v1::ClientFrame first, std::optional<coro::MpscReceiver<v1::ClientFrame>> in,
                                          size_t input_queue) {
    if (first.has_request()) co_yield parse<Request>(first.request());
    if (first.end()) co_return;
    for (;;) {
        auto frame = co_await in->recv();
        if (!frame) {
            throw CallError(v1::Error::RESOURCE_EXHAUSTED, "the client sent more than " +
                                                               std::to_string(input_queue) +
                                                               " requests ahead of the call");
        }
        if (frame->has_request()) co_yield parse<Request>(frame->request());
        if (frame->end()) co_return;
    }
}

template<typename Request>
coro::CoroStream<Request> client_requests(Call& call) {
    return client_requests<Request>(std::move(call.first), std::move(call.in), call.input_queue);
}

// A streaming handler's items, each sent before it's asked for the next, so a client that reads slowly holds the
// handler back at its co_yield.
template<typename Items>
coro::Coro<void> send_items(Call& call, Items items) {
    while (auto item = co_await coro::next(items)) {
        v1::ServerFrame frame;
        frame.set_id(call.id);
        frame.set_reply(item->SerializeAsString());
        if (!co_await send(call.out, frame)) co_return;
    }
}

// The adapters' calls, one per kind. Each keeps its request, or its stream of requests, in its own frame, for
// the handler to read.

template<auto Handler>
coro::Coro<std::optional<std::string>> run_unary(Service& service, Call call) {
    using H = HandlerOf<decltype(Handler)>;
    const typename H::Param request = single_request<typename H::Param>(call);
    log_started(call, false, request.ShortDebugString());
    // Named, not awaited in place: GCC 15 crashes on a co_await of a call through a member pointer.
    auto& handlers = static_cast<typename H::Class&>(service);
    auto pending = (handlers.*Handler)(request);
    auto reply = co_await pending;
    co_return reply.SerializeAsString();
}

template<auto Handler>
coro::Coro<std::optional<std::string>> run_server_streaming(Service& service, Call call) {
    using H = HandlerOf<decltype(Handler)>;
    const typename H::Param request = single_request<typename H::Param>(call);
    log_started(call, true, request.ShortDebugString());
    auto& handlers = static_cast<typename H::Class&>(service);
    auto items = (handlers.*Handler)(request);  // named, as in run_unary()
    co_await send_items(call, std::move(items));
    co_return std::nullopt;
}

template<auto Handler>
coro::Coro<std::optional<std::string>> run_client_streaming(Service& service, Call call) {
    using H = HandlerOf<decltype(Handler)>;
    using Request = typename IsCoroStream<typename H::Param>::Value;
    log_started(call, true, "");
    auto& handlers = static_cast<typename H::Class&>(service);
    auto pending = (handlers.*Handler)(client_requests<Request>(call));  // named, as in run_unary()
    auto reply = co_await pending;
    co_return reply.SerializeAsString();
}

template<auto Handler>
coro::Coro<std::optional<std::string>> run_bidi(Service& service, Call call) {
    using H = HandlerOf<decltype(Handler)>;
    using Request = typename IsCoroStream<typename H::Param>::Value;
    log_started(call, true, "");
    auto& handlers = static_cast<typename H::Class&>(service);
    auto items = (handlers.*Handler)(client_requests<Request>(call));  // named, as in run_unary()
    co_await send_items(call, std::move(items));
    co_return std::nullopt;
}

}  // namespace detail

// The adapters the generated method tables name, one per kind of call. Each checks the handler's signature is
// its kind's, so a mistake in the generator is an error here.

template<auto Handler>
Method unary(std::string_view name) {
    using H = detail::HandlerOf<decltype(Handler)>;
    static_assert(detail::IsCoro<typename H::Result>::value && !detail::IsCoroStream<typename H::Param>::value,
                  "a unary handler is coro::Coro<Reply>(const Request&)");
    return {name, Method::Kind::Unary, &detail::run_unary<Handler>};
}

template<auto Handler>
Method server_streaming(std::string_view name) {
    using H = detail::HandlerOf<decltype(Handler)>;
    static_assert(detail::IsCoroStream<typename H::Result>::value && !detail::IsCoroStream<typename H::Param>::value,
                  "a server-streaming handler is coro::CoroStream<Reply>(const Request&)");
    return {name, Method::Kind::ServerStreaming, &detail::run_server_streaming<Handler>};
}

template<auto Handler>
Method client_streaming(std::string_view name) {
    using H = detail::HandlerOf<decltype(Handler)>;
    static_assert(detail::IsCoro<typename H::Result>::value && detail::IsCoroStream<typename H::Param>::value,
                  "a client-streaming handler is coro::Coro<Reply>(coro::CoroStream<Request>)");
    return {name, Method::Kind::ClientStreaming, &detail::run_client_streaming<Handler>};
}

template<auto Handler>
Method bidi(std::string_view name) {
    using H = detail::HandlerOf<decltype(Handler)>;
    static_assert(detail::IsCoroStream<typename H::Result>::value && detail::IsCoroStream<typename H::Param>::value,
                  "a bidirectional handler is coro::CoroStream<Reply>(coro::CoroStream<Request>)");
    return {name, Method::Kind::Bidi, &detail::run_bidi<Handler>};
}

}  // namespace wsrpc
