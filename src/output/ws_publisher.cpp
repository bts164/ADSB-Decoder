#include "output/ws_publisher.h"

#include <csignal>
#include <cstdlib>
#include <deque>
#include <optional>

#include <coro/coro.h>
#include <coro/io/ws_listener.h>
#include <coro/io/ws_stream.h>
#include <coro/sync/broadcast.h>
#include <coro/sync/interval.h>
#include <coro/sync/join.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/select.h>
#include <coro/sync/timeout.h>
#include <coro/sync/when.h>
#include <coro/task/join_set.h>
#include <coro/task/spawn_blocking.h>
#include <nlohmann/json.hpp>

#include "common/log.h"
#include "output/aircraft_history.h"

namespace {

// Static-duration, so publish() is a safe no-op for the entire process
// lifetime until ws_publisher_init() runs.
std::optional<coro::BroadcastSender<std::string>> g_tx;

// Most published text one client may have waiting (Pending) unless ADSB_WS_MAX_PENDING_BYTES says otherwise.
// Far more than a client that keeps up ever holds: about 0.5 MB/s is published with 1000 frames/s and the
// spectrum on.
constexpr size_t kDefaultMaxPendingBytes = 16u << 20;

struct Settings {
    std::chrono::nanoseconds update_interval;
    size_t max_pending_bytes;
};

size_t max_pending_bytes_from_env() {
    const char* env = std::getenv("ADSB_WS_MAX_PENDING_BYTES");
    if (!env) return kDefaultMaxPendingBytes;
    char* end = nullptr;
    const unsigned long long n = std::strtoull(env, &end, 10);
    if (end == env || *end != '\0' || n == 0) {
        LOGF(WARNING, "ignoring ADSB_WS_MAX_PENDING_BYTES=%s: not a positive byte count", env);
        return kDefaultMaxPendingBytes;
    }
    return static_cast<size_t>(n);
}

// Sends one message, giving up on it (not the client) after 2 s. Throws if the client is gone.
coro::Coro<void> send_or_skip(coro::WsStream& ws, const std::string& msg) {
    auto result = co_await coro::timeout(std::chrono::seconds(2), ws.send(msg));
    if (result.index() != 0) LOGF(WARNING, "websocket client timeout");
}

// Answers the client's requests by queuing replies for send_updates(): `{"type":"seek","t":<unix s>}` gets
// aircraft_history_snapshot(t) and `{"type":"aircraft_history","icao":"<hex>","t":<unix s, optional>}` gets
// aircraft_history_aircraft(icao, t), and `{"type":"activity","from":<unix s, optional>}` gets
// aircraft_history_activity(from), and `{"type":"heatmap","west":...,"south":...,"east":...,"north":...,
// "width":...,"height":...,"t0":...,"t1":...}` (times optional) gets aircraft_history_heatmap(). Runs until the
// client disconnects, when receive() throws.
coro::Coro<void> read_requests(coro::WsStream& ws, coro::MpscSender<std::string> replies) {
    for (;;) {
        auto msg = co_await ws.receive();
        if (!msg.is_text) continue;
        auto req = nlohmann::json::parse(msg.as_text(), nullptr, false);
        const std::string type = req.is_object() ? req.value("type", "") : "";
        std::optional<std::string> reply;
        if (type == "seek" && req["t"].is_number()) {
            const double t = req["t"].get<double>();
            reply = co_await coro::spawn_blocking([t] { return aircraft_history_snapshot(t); });
        } else if (type == "aircraft_history" && req["icao"].is_string()) {
            std::string icao = req["icao"].get<std::string>();
            std::optional<double> t;
            if (req["t"].is_number()) t = req["t"].get<double>();
            reply = co_await coro::spawn_blocking([icao, t] { return aircraft_history_aircraft(icao, t); });
        } else if (type == "activity") {
            std::optional<double> from;
            if (req["from"].is_number()) from = req["from"].get<double>();
            reply = co_await coro::spawn_blocking([from] { return aircraft_history_activity(from); });
        } else if (type == "heatmap" && req["west"].is_number() && req["south"].is_number() &&
                   req["east"].is_number() && req["north"].is_number() && req["width"].is_number_integer() &&
                   req["height"].is_number_integer()) {
            HeatmapRequest heat{req["west"].get<double>(), req["south"].get<double>(), req["east"].get<double>(),
                                req["north"].get<double>(), req["width"].get<int>(), req["height"].get<int>(),
                                std::nullopt, std::nullopt};
            if (req["t0"].is_number()) heat.t0 = req["t0"].get<double>();
            if (req["t1"].is_number()) heat.t1 = req["t1"].get<double>();
            reply = co_await coro::spawn_blocking([heat] { return aircraft_history_heatmap(heat); });
        } else {
            LOGF(WARNING, "ignoring websocket client message: %s", msg.as_text());
            continue;
        }
        if (reply) co_await replies.send(std::move(*reply));
    }
}

// Published text waiting for one client's next update: the ws_publisher_publish() strings received since the
// last one, oldest first. Shared by collect_messages() and send_updates() without a lock, because both run
// in the client's one task (under handle_client()'s join()) and neither holds a reference across a co_await.
struct Pending {
    std::deque<std::string> parts;
    size_t bytes = 0;  // total size of `parts`
};

// Moves every published string into `pending` as it arrives, so the client's broadcast receiver is drained
// however slow its socket is. Over settings.max_pending_bytes the oldest strings are dropped. Runs until the
// channel closes.
coro::Coro<void> collect_messages(coro::BroadcastReceiver<std::string> rx, Pending& pending, Settings settings) {
    for (;;) {
        auto r = co_await rx.recv();
        if (!r) {
            if (r.error().kind != coro::BroadcastRecvError::Kind::Lagged) co_return;
            LOGF(WARNING, "websocket client lagged");
            continue;
        }
        pending.bytes += r.value().size();
        pending.parts.push_back(std::move(r.value()));
        size_t dropped = 0;
        while (pending.bytes > settings.max_pending_bytes && pending.parts.size() > 1) {
            pending.bytes -= pending.parts.front().size();
            pending.parts.pop_front();
            ++dropped;
        }
        if (dropped > 0)
            LOGF(WARNING, "websocket client is over %u pending bytes: dropped %u update(s)",
                 settings.max_pending_bytes, dropped);
    }
}

// The client's only writer. Every settings.update_interval it sends what collect_messages() gathered as one
// JSON array, and it sends each reply read_requests() queues as soon as it arrives. Runs until the replies
// channel closes or a send fails.
coro::Coro<void> send_updates(coro::WsStream& ws, Pending& pending, coro::MpscReceiver<std::string> replies,
                              Settings settings) {
    coro::IntervalTimer timer(settings.update_interval);
    for (;;) {
        // A reply that wins leaves the timer as it was, so replies don't push the next update back.
        auto result = co_await coro::select(replies.recv(), timer.tick());
        if (result.index() == 0) {
            auto& r = std::get<0>(result).value;
            if (!r) co_return;
            co_await send_or_skip(ws, *r);
            continue;
        }
        if (pending.parts.empty()) continue;
        // Taken out of `pending` before the send suspends: collect_messages() keeps adding during it.
        std::string msg;
        msg.reserve(pending.bytes + pending.parts.size() + 1);
        char sep = '[';
        for (const std::string& part : pending.parts) {
            msg += sep;
            msg += part;
            sep = ',';
        }
        msg += ']';
        pending.parts.clear();
        pending.bytes = 0;
        co_await send_or_skip(ws, msg);
    }
}

coro::Coro<void> handle_client(coro::WsStream ws, coro::BroadcastReceiver<std::string> rx, Settings settings) {
    try {
        // Backfill this one client with the latest history snapshot before it joins the live broadcast --
        // a per-connection send, not a broadcast, so late joiners don't replay history at everyone else.
        if (auto snapshot = co_await coro::spawn_blocking([] { return aircraft_history_snapshot(std::nullopt); }))
            co_await send_or_skip(ws, *snapshot);

        Pending pending;
        auto [replies_tx, replies_rx] = coro::mpsc_channel<std::string>(4);
        co_await coro::join(read_requests(ws, std::move(replies_tx)),
                            collect_messages(std::move(rx), pending, settings),
                            send_updates(ws, pending, std::move(replies_rx), settings));
    } catch (const std::exception& e) {
        LOGF(INFO, "websocket client ended: %s", e.what());
    }
}

coro::Coro<void> run(uint16_t port, coro::BroadcastReceiver<std::string> rx, Settings settings) {
    coro::WsListener listener = co_await coro::WsListener::bind("0.0.0.0", port);
    LOGF(INFO, "websocket listening on port %u", port);
    // Runs until the task is cancelled: Ctrl+C is handled in main(), and ~Runtime cancels and drains this
    // task (see ws_publisher_init()). JoinSet's destructor then cancels and drains any remaining clients.
    coro::JoinSet<void> clients;
    for (;;) {
        try {
            // clients starts empty and coro::next() on an empty JoinSet resolves
            // immediately (end-of-stream) -- when() skips constructing/polling that
            // branch entirely until there's actually a client to wait on, so this
            // select() suspends properly instead of spinning.
            auto result = co_await coro::select(
                listener.accept(),
                coro::when(!clients.empty(), [&] { return coro::next(clients); }));
            if (result.index() == 0) {
                auto &ws = std::get<0>(result).value;
                clients.spawn(handle_client(std::move(ws), rx.resubscribe(), settings));
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

void ws_publisher_init(uint16_t port, std::chrono::nanoseconds update_interval) {
    auto channel = coro::broadcast_channel<std::string>(64);
    g_tx = std::move(channel.first);
    // Detached, not kept as a member/global: run() loops forever (accepting
    // clients until the process exits), so there's no natural point to
    // cancel it from and nothing to join -- keeping its JoinHandle around
    // as a static would instead have it torn down at static-destructor time,
    // which runs after main()'s coro::Runtime is already gone. Runtime's own
    // destructor is what actually cancels and drains this task, while the
    // runtime is still alive to do so.
    coro::spawn(run(port, std::move(channel.second), Settings{update_interval, max_pending_bytes_from_env()}))
        .detach();
}

void ws_publisher_publish(std::string json_objects) {
    if (g_tx) g_tx->send(std::move(json_objects));
}
