#include "output/ws_publisher.h"

#include <csignal>
#include <iostream>
#include <optional>

#include <coro/coro.h>
#include <coro/io/signal.h>
#include <coro/io/ws_listener.h>
#include <coro/io/ws_stream.h>
#include <coro/sync/broadcast.h>
#include <coro/sync/join.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/select.h>
#include <coro/sync/timeout.h>
#include <coro/task/join_set.h>
#include <coro/task/spawn_blocking.h>
#include <nlohmann/json.hpp>

#include "output/aircraft_history.h"

namespace {

// Static-duration, so publish() is a safe no-op for the entire process
// lifetime until ws_publisher_init() runs.
std::optional<coro::BroadcastSender<std::string>> g_tx;

// Sends one message, giving up on it (not the client) after 2 s. Throws if the client is gone.
coro::Coro<void> send_or_skip(coro::WsStream& ws, const std::string& msg) {
    auto result = co_await coro::timeout(std::chrono::seconds(2), ws.send(msg));
    if (result.index() != 0) std::cerr << "[ws] client timeout\n";
}

// Answers the client's requests by queuing replies for write_messages(): `{"type":"seek","t":<unix s>}` gets
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
            std::cerr << "[ws] ignoring client message: " << msg.as_text() << "\n";
            continue;
        }
        if (reply) co_await replies.send(std::move(*reply));
    }
}

// Sends the client every broadcast line and every reply read_requests() queues, until either ends or a
// send fails.
coro::Coro<void> write_messages(coro::WsStream& ws, coro::BroadcastReceiver<std::string> rx,
                                coro::MpscReceiver<std::string> replies) {
    for (;;) {
        auto result = co_await coro::select(rx.recv(), replies.recv());
        std::string msg;
        if (result.index() == 0) {
            auto& r = std::get<0>(result).value;
            if (!r) {
                if (r.error().kind != coro::BroadcastRecvError::Kind::Lagged) co_return;
                std::cerr << "[ws] client lagged\n";
                continue;
            }
            msg = std::move(r.value());
        } else {
            auto& r = std::get<1>(result).value;
            if (!r) co_return;
            msg = std::move(*r);
        }
        co_await send_or_skip(ws, msg);
    }
}

coro::Coro<void> handle_client(coro::WsStream ws, coro::BroadcastReceiver<std::string> rx) {
    try {
        // Backfill this one client with the latest history snapshot before it joins the live broadcast --
        // a per-connection send, not a broadcast, so late joiners don't replay history at everyone else.
        if (auto snapshot = co_await coro::spawn_blocking([] { return aircraft_history_snapshot(std::nullopt); }))
            co_await send_or_skip(ws, *snapshot);

        auto [replies_tx, replies_rx] = coro::mpsc_channel<std::string>(4);
        co_await coro::join(read_requests(ws, std::move(replies_tx)),
                            write_messages(ws, std::move(rx), std::move(replies_rx)));
    } catch (const std::exception& e) {
        std::cerr << "[ws] client ended: " << e.what() << "\n";
    }
}

coro::Coro<void> run(uint16_t port, coro::BroadcastReceiver<std::string> rx) {
    coro::WsListener listener = co_await coro::WsListener::bind("0.0.0.0", port);
    std::cerr << "[ws] listening on port " << port << "\n";
    coro::JoinSet<void> clients;
    clients.spawn([] -> coro::Coro<void> {
        co_await coro::signal(SIGINT);
    }());
    for (;;) {
        try {
            auto result = co_await coro::select(listener.accept(), coro::next(clients));
            if (result.index() == 0) {
                auto &ws = std::get<0>(result).value;
                clients.spawn(handle_client(std::move(ws), rx.resubscribe()));
                std::cerr << "[ws] client connected\n";
            } else {
                std::cerr << "[ws] client disconnected\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[ws] error: " << e.what() << "\n";
        }
    }
}

}  // namespace

void ws_publisher_init(uint16_t port) {
    auto channel = coro::broadcast_channel<std::string>(64);
    g_tx = std::move(channel.first);
    // Detached, not kept as a member/global: run() loops forever (accepting
    // clients until the process exits), so there's no natural point to
    // cancel it from and nothing to join -- keeping its JoinHandle around
    // as a static would instead have it torn down at static-destructor time,
    // which runs after main()'s coro::Runtime is already gone. Runtime's own
    // destructor is what actually cancels and drains this task, while the
    // runtime is still alive to do so.
    coro::spawn(run(port, std::move(channel.second))).detach();
}

void ws_publisher_publish(std::string json_line) {
    if (g_tx) g_tx->send(std::move(json_line));
}
