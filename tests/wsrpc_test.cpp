// Tests for the wsrpc runtime, as a client sees it: each test serves tests/wsrpc_test.proto's Echo on a port of
// its own, connects over a WebSocket, and makes calls of each kind, sending the frames by hand.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

#include <absl/log/initialize.h>
#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/io/ws_listener.h>
#include <coro/io/ws_stream.h>
#include <coro/runtime/runtime.h>
#include <coro/stream.h>
#include <coro/sync/select.h>
#include <coro/sync/sleep.h>
#include <coro/sync/timeout.h>
#include <coro/sync/when.h>
#include <coro/task/join_set.h>
#include <coro/task/spawn_builder.h>

#include "wsrpc/serve.h"
#include "wsrpc_test.wsrpc.h"

namespace {

using namespace std::chrono_literals;
using wsrpc::v1::ClientFrame;
using wsrpc::v1::Error;
using wsrpc::v1::ServerFrame;
using wsrpc_test::v1::CountRequest;
using wsrpc_test::v1::EchoReply;
using wsrpc_test::v1::EchoRequest;

int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

EchoReply reply(int64_t value) {
    EchoReply r;
    r.set_value(value);
    return r;
}

class EchoHandlers final : public wsrpc_test::v1::Echo {
public:
    // An echo of this fails as a handler's bug would; one of a negative value fails with a CallError.
    static constexpr int64_t kThrows = 999;
    // How long sum() waits before it reads its requests.
    static constexpr auto kSumDelay = 200ms;

    coro::Coro<EchoReply> echo(const EchoRequest& request) override {
        if (request.value() < 0) throw wsrpc::CallError(Error::FAILED_PRECONDITION, "a negative value");
        if (request.value() == kThrows) throw std::runtime_error("a handler's bug");
        co_return reply(request.value());
    }

    coro::CoroStream<EchoReply> count(const CountRequest& request) override {
        if (request.count() == 0) {
            for (int64_t v = request.value();; v++) {
                co_yield reply(v);
                co_await coro::sleep_for(10ms);
            }
        }
        for (uint32_t i = 0; i < request.count(); i++) co_yield reply(request.value() + i);
    }

    coro::Coro<EchoReply> sum(coro::CoroStream<EchoRequest> requests) override {
        co_await coro::sleep_for(kSumDelay);
        int64_t total = 0;
        while (auto r = co_await coro::next(requests)) total += r->value();
        co_return reply(total);
    }

    coro::CoroStream<EchoReply> echo_each(coro::CoroStream<EchoRequest> requests) override {
        while (auto r = co_await coro::next(requests)) co_yield reply(r->value());
    }
};

// The connection's handlers are its task's own, as a server's would be.
coro::Coro<void> serve_client(coro::WsStream ws) {
    EchoHandlers echo;
    co_await wsrpc::serve(std::move(ws), {&echo});
}

coro::Coro<void> listen(uint16_t port) {
    coro::WsListener listener = co_await coro::WsListener::bind("127.0.0.1", port);
    coro::JoinSet<void> clients;
    for (;;) {
        // when() skips the JoinSet until it has a client: next() on an empty one resolves at once.
        auto woke =
            co_await coro::select(listener.accept(), coro::when(!clients.empty(), [&] { return coro::next(clients); }));
        if (woke.index() == 0)
            clients.build_task(serve_client(std::move(std::get<0>(woke).value))).name("ws_client").spawn();
    }
}

// The server starts listening in a task of its own, so the first tries may be refused.
coro::Coro<coro::WsStream> connect(uint16_t port) {
    const std::string url = "ws://127.0.0.1:" + std::to_string(port) + "/";
    for (int attempt = 1;; attempt++) {
        bool refused = false;
        try {
            co_return co_await coro::WsStream::connect(url);
        } catch (const std::system_error&) {
            if (attempt == 50) throw;
            refused = true;
        }
        if (refused) co_await coro::sleep_for(20ms);
    }
}

coro::Coro<void> send(coro::WsStream& ws, const ClientFrame& frame) {
    const std::string s = frame.SerializeAsString();
    co_await ws.send(std::as_bytes(std::span(s)));
}

ClientFrame hello(std::initializer_list<std::string_view> services) {
    ClientFrame frame;
    for (std::string_view s : services) frame.mutable_hello()->add_services(std::string(s));
    return frame;
}

std::string echo_method(std::string_view rpc) {
    return "/" + std::string(wsrpc_test::v1::Echo::kName) + "/" + std::string(rpc);
}

// A call's first frame, to `method`, with `request` unless it's unset.
ClientFrame start(uint32_t id, const std::string& method, std::optional<std::string> request, bool end = false) {
    ClientFrame frame;
    frame.set_id(id);
    frame.set_method(method);
    if (request) frame.set_request(std::move(*request));
    frame.set_end(end);
    return frame;
}

ClientFrame echo(uint32_t id, int64_t value) {
    EchoRequest r;
    r.set_value(value);
    return start(id, echo_method("Echo"), r.SerializeAsString());
}

ClientFrame count(uint32_t id, int64_t value, uint32_t n) {
    CountRequest r;
    r.set_value(value);
    r.set_count(n);
    return start(id, echo_method("Count"), r.SerializeAsString());
}

// A client stream's later frame.
ClientFrame later(uint32_t id, std::optional<int64_t> value, bool end = false) {
    ClientFrame frame;
    frame.set_id(id);
    if (value) {
        EchoRequest r;
        r.set_value(*value);
        frame.set_request(r.SerializeAsString());
    }
    frame.set_end(end);
    return frame;
}

ClientFrame cancel(uint32_t id) {
    ClientFrame frame;
    frame.set_id(id);
    frame.set_cancel(true);
    return frame;
}

// The next frame from the server, or none within `wait`.
coro::Coro<std::optional<ServerFrame>> receive(coro::WsStream& ws, std::chrono::nanoseconds wait = 2s) {
    auto result = co_await coro::timeout(wait, ws.receive());
    if (result.index() != 0) co_return std::nullopt;
    const coro::WsStream::Message& bytes = std::get<0>(result).value;
    ServerFrame frame;
    if (bytes.is_text || !frame.ParseFromArray(bytes.data.data(), static_cast<int>(bytes.data.size())))
        co_return std::nullopt;
    co_return frame;
}

// Whether the server closes the connection within a couple of seconds.
coro::Coro<bool> closed(coro::WsStream& ws) {
    try {
        (void)co_await coro::timeout(2s, ws.receive());
        co_return false;
    } catch (const std::exception&) {
        co_return true;
    }
}

// The value of a frame's reply, if it's of call `id` and has one.
std::optional<int64_t> value_of(const std::optional<ServerFrame>& frame, uint32_t id) {
    EchoReply r;
    if (!frame || frame->id() != id || !frame->has_reply() || !r.ParseFromString(frame->reply())) return std::nullopt;
    return r.value();
}

bool is_end(const std::optional<ServerFrame>& frame, uint32_t id) {
    return frame && frame->id() == id && frame->end() && !frame->has_error();
}

bool is_error(const std::optional<ServerFrame>& frame, uint32_t id, Error::Code code) {
    return frame && frame->id() == id && frame->end() && frame->has_error() && frame->error().code() == code &&
           !frame->has_reply();
}

// Says the client's hello, knowing Echo, and checks the answer.
coro::Coro<void> greet(coro::WsStream& ws) {
    co_await send(ws, hello({wsrpc_test::v1::Echo::kName}));
    auto answer = co_await receive(ws);
    CHECK(answer && answer->id() == 0 && answer->has_hello());
}

// Serves Echo on `port` and runs `test` as its client, after the hello unless `greeted` is false. The runtime's
// shutdown stops the server.
template<typename Test>
void run(uint16_t port, Test test, bool greeted = true) {
    coro::Runtime(2).block_on([](uint16_t port, Test test, bool greeted) -> coro::Coro<void> {
        coro::build_task().name("ws_accept").spawn(listen(port)).detach();
        coro::WsStream ws = co_await connect(port);
        if (greeted) co_await greet(ws);
        co_await test(ws, port);
    }(port, test, greeted));
}

void test_hello() {
    run(18801, [](coro::WsStream& ws, uint16_t) -> coro::Coro<void> {
        co_await send(ws, hello({wsrpc_test::v1::Echo::kName, wsrpc_test::v1::Shapes::kName}));
        auto answer = co_await receive(ws);
        CHECK(answer && answer->id() == 0 && answer->has_hello() && answer->hello().services_size() == 1);
        CHECK(answer && answer->hello().services(0) == wsrpc_test::v1::Echo::kName);
    }, false);
}

void test_call_before_hello() {
    run(18802, [](coro::WsStream& ws, uint16_t) -> coro::Coro<void> {
        co_await send(ws, echo(1, 5));
        CHECK(co_await closed(ws));
    }, false);
}

void test_unary() {
    run(18803, [](coro::WsStream& ws, uint16_t) -> coro::Coro<void> {
        co_await send(ws, echo(1, 5));
        auto frame = co_await receive(ws);
        CHECK(is_end(frame, 1) && value_of(frame, 1) == 5);
        // A request with every field at its default is no bytes, but present.
        co_await send(ws, echo(2, 0));
        frame = co_await receive(ws);
        CHECK(is_end(frame, 2) && value_of(frame, 2) == 0);
    });
}

void test_server_stream() {
    run(18804, [](coro::WsStream& ws, uint16_t) -> coro::Coro<void> {
        co_await send(ws, count(1, 10, 3));
        for (int64_t v = 10; v < 13; v++) {
            auto frame = co_await receive(ws);
            CHECK(frame && !frame->end() && value_of(frame, 1) == v);
        }
        auto last = co_await receive(ws);
        CHECK(is_end(last, 1) && !last->has_reply());
    });
}

void test_client_stream() {
    run(18805, [](coro::WsStream& ws, uint16_t) -> coro::Coro<void> {
        EchoRequest one;
        one.set_value(1);
        co_await send(ws, start(1, echo_method("Sum"), one.SerializeAsString()));
        co_await send(ws, later(1, 2));
        co_await send(ws, later(1, 3, true));
        auto frame = co_await receive(ws);
        CHECK(is_end(frame, 1) && value_of(frame, 1) == 6);

        // The first frame's `end`: just the one request.
        EchoRequest four;
        four.set_value(4);
        co_await send(ws, start(2, echo_method("Sum"), four.SerializeAsString(), true));
        frame = co_await receive(ws);
        CHECK(is_end(frame, 2) && value_of(frame, 2) == 4);

        // An `end` with no request, after a first frame with none: no requests at all.
        co_await send(ws, start(3, echo_method("Sum"), std::nullopt));
        co_await send(ws, later(3, std::nullopt, true));
        frame = co_await receive(ws);
        CHECK(is_end(frame, 3) && value_of(frame, 3) == 0);
    });
}

void test_bidi() {
    run(18806, [](coro::WsStream& ws, uint16_t) -> coro::Coro<void> {
        co_await send(ws, start(1, echo_method("EchoEach"), std::nullopt));
        co_await send(ws, later(1, 7));
        auto frame = co_await receive(ws);
        CHECK(frame && !frame->end() && value_of(frame, 1) == 7);
        co_await send(ws, later(1, 8, true));
        frame = co_await receive(ws);
        CHECK(frame && !frame->end() && value_of(frame, 1) == 8);
        auto last = co_await receive(ws);
        CHECK(is_end(last, 1) && !last->has_reply());
    });
}

void test_errors() {
    run(18807, [](coro::WsStream& ws, uint16_t) -> coro::Coro<void> {
        co_await send(ws, echo(1, -1));
        CHECK(is_error(co_await receive(ws), 1, Error::FAILED_PRECONDITION));
        co_await send(ws, echo(2, EchoHandlers::kThrows));
        CHECK(is_error(co_await receive(ws), 2, Error::INTERNAL));
        co_await send(ws, start(3, echo_method("Echo"), std::string("\xff")));
        CHECK(is_error(co_await receive(ws), 3, Error::INVALID_ARGUMENT));
        co_await send(ws, start(4, echo_method("Echo"), std::nullopt));
        CHECK(is_error(co_await receive(ws), 4, Error::INVALID_ARGUMENT));
        co_await send(ws, start(5, echo_method("Nope"), ""));
        CHECK(is_error(co_await receive(ws), 5, Error::UNIMPLEMENTED));
        // Shapes is in the .proto, but this server doesn't serve it.
        co_await send(ws, start(6, "/" + std::string(wsrpc_test::v1::Shapes::kName) + "/GetURLList", ""));
        CHECK(is_error(co_await receive(ws), 6, Error::UNIMPLEMENTED));
        co_await send(ws, start(7, "Echo", ""));
        CHECK(is_error(co_await receive(ws), 7, Error::UNIMPLEMENTED));
        // The connection carries on after them all.
        co_await send(ws, echo(8, 1));
        CHECK(value_of(co_await receive(ws), 8) == 1);
    });
}

void test_cancel() {
    run(18808, [](coro::WsStream& ws, uint16_t) -> coro::Coro<void> {
        co_await send(ws, count(1, 0, 0));
        CHECK(value_of(co_await receive(ws), 1) == 0);
        co_await send(ws, cancel(1));
        // Replies already on their way come first.
        std::optional<ServerFrame> frame;
        for (int i = 0; i < 20; i++) {
            frame = co_await receive(ws);
            if (!frame || frame->end()) break;
        }
        CHECK(is_error(frame, 1, Error::CANCELLED));
        CHECK(!co_await receive(ws, 200ms));
    });
}

void test_cancel_after_end() {
    run(18809, [](coro::WsStream& ws, uint16_t) -> coro::Coro<void> {
        co_await send(ws, echo(1, 1));
        CHECK(is_end(co_await receive(ws), 1));
        co_await send(ws, cancel(1));
        CHECK(!co_await receive(ws, 100ms));
    });
}

void test_call_cap() {
    run(18810, [](coro::WsStream& ws, uint16_t) -> coro::Coro<void> {
        const uint32_t cap = wsrpc::Limits{}.client_calls;
        // Client streams with no `end` stay in progress and send nothing.
        for (uint32_t id = 1; id <= cap; id++) co_await send(ws, start(id, echo_method("Sum"), std::nullopt));
        co_await send(ws, echo(cap + 1, 1));
        CHECK(is_error(co_await receive(ws), cap + 1, Error::RESOURCE_EXHAUSTED));
        // A cancel isn't a call, so the cap doesn't refuse it, and a cancelled call frees its place.
        co_await send(ws, cancel(3));
        CHECK(is_error(co_await receive(ws), 3, Error::CANCELLED));
        co_await send(ws, echo(cap + 2, 1));
        CHECK(value_of(co_await receive(ws), cap + 2) == 1);
    });
}

void test_input_overflow() {
    run(18811, [](coro::WsStream& ws, uint16_t) -> coro::Coro<void> {
        const size_t queue = wsrpc::Limits{}.call_input_queue;
        // All sent while sum() waits, before it reads any: the first frame's, then one more than the queue holds.
        co_await send(ws, start(1, echo_method("Sum"), std::nullopt));
        for (size_t i = 0; i <= queue; i++) co_await send(ws, later(1, 1));
        CHECK(is_error(co_await receive(ws), 1, Error::RESOURCE_EXHAUSTED));
    });
}

void test_repeated_id() {
    run(18812, [](coro::WsStream& ws, uint16_t) -> coro::Coro<void> {
        co_await send(ws, echo(5, 1));
        co_await send(ws, echo(5, 2));
        co_await send(ws, echo(4, 3));
        // The refusals come from the connection's reader, and the reply from the call's task, so either can be
        // first.
        int replies = 0, refusals = 0;
        for (int i = 0; i < 3; i++) {
            auto frame = co_await receive(ws);
            if (value_of(frame, 5) == 1) replies++;
            if (is_error(frame, 5, Error::INVALID_ARGUMENT) || is_error(frame, 4, Error::INVALID_ARGUMENT))
                refusals++;
        }
        CHECK(replies == 1 && refusals == 2);
    });
}

void test_late_frames() {
    run(18813, [](coro::WsStream& ws, uint16_t) -> coro::Coro<void> {
        co_await send(ws, echo(1, 1));
        CHECK(is_end(co_await receive(ws), 1));
        // To a call that has ended, and to one in progress that takes no more input.
        co_await send(ws, later(1, 2, true));
        co_await send(ws, count(2, 0, 0));
        co_await send(ws, later(2, 2, true));
        co_await send(ws, cancel(2));
        std::optional<ServerFrame> frame;
        for (int i = 0; i < 20; i++) {
            frame = co_await receive(ws);
            if (!frame || frame->end()) break;
            CHECK(frame->id() == 2);
        }
        CHECK(is_error(frame, 2, Error::CANCELLED));
        CHECK(!co_await receive(ws, 100ms));
    });
}

void test_garbage() {
    run(18814, [](coro::WsStream& ws, uint16_t) -> coro::Coro<void> {
        co_await ws.send(std::string_view("a text frame"));
        const std::string bytes = "\xff\xff";
        co_await ws.send(std::as_bytes(std::span(bytes)));
        CHECK(!co_await receive(ws, 100ms));
        co_await send(ws, echo(1, 1));
        CHECK(value_of(co_await receive(ws), 1) == 1);
    });
}

void test_disconnect() {
    run(18815, [](coro::WsStream& ws, uint16_t port) -> coro::Coro<void> {
        co_await send(ws, count(1, 0, 0));
        co_await send(ws, start(2, echo_method("Sum"), std::nullopt));
        CHECK(value_of(co_await receive(ws), 1) == 0);
        ws = co_await connect(port);  // closes the first connection
        co_await greet(ws);
        co_await send(ws, echo(1, 1));
        CHECK(value_of(co_await receive(ws), 1) == 1);
    });
}

}  // namespace

int main() {
    absl::InitializeLog();
    test_hello();
    test_call_before_hello();
    test_unary();
    test_server_stream();
    test_client_stream();
    test_bidi();
    test_errors();
    test_cancel();
    test_cancel_after_end();
    test_call_cap();
    test_input_overflow();
    test_repeated_id();
    test_late_frames();
    test_garbage();
    test_disconnect();
    std::printf(g_failures ? "%d FAILURE(S)\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
