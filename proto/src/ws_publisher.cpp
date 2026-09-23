#include "ws_publisher.h"

#include <csignal>
#include <iostream>
#include <optional>

#include <coro/coro.h>
#include <coro/io/signal.h>
#include <coro/io/ws_listener.h>
#include <coro/io/ws_stream.h>
#include <coro/sync/broadcast.h>
#include <coro/sync/select.h>
#include <coro/sync/timeout.h>
#include <coro/task/join_set.h>

#include "aircraft_history.h"

namespace {

// Static-duration, so publish() is a safe no-op for the entire process
// lifetime until ws_publisher_init() runs.
std::optional<coro::BroadcastSender<std::string>> g_tx;

coro::Coro<void> handle_client(coro::WsStream ws, coro::BroadcastReceiver<std::string> rx) {
    // Backfill this one client's aircraft history before it joins the live
    // broadcast -- a per-connection send, not a broadcast, so late joiners
    // don't replay history at everyone else too.
    for (const auto& msg : aircraft_history_backfill_messages()) {
        try {
            auto send_result = co_await coro::timeout(std::chrono::seconds(2), ws.send(msg));
            if (send_result.index() != 0) {
                std::cerr << "[ws] client history backfill timeout\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[ws] client history backfill failed: " << e.what() << "\n";
            co_return;
        }
    }

    std::expected<std::string, coro::BroadcastRecvError> msg;
    while (true) {
        auto result = co_await coro::select(ws.receive(), rx.recv());
        if (result.index() == 0) {
            std::cerr << "[ws] client received a message\n";
            // TODO: handle ws msg
        } else {
            auto &rx_result = std::get<1>(result).value;
            if (!rx_result) {
                if (rx_result.error().kind == coro::BroadcastRecvError::Kind::Lagged) {
                    std::cerr << "[ws] client lagged\n";
                } else {
                    co_return;
                }
            } else {
                auto const &msg = rx_result.value();
                try {
                    auto send_result =
                        co_await coro::timeout(std::chrono::seconds(2), ws.send(msg));
                    if (send_result.index() != 0) {
                        std::cerr << "[ws] client timeout\n";
                    }
                } catch (const std::exception &e) {
                    std::cerr << "[ws] client failed: " << e.what() << "\n";
                    co_return;
                }
            }
        }
    }
    co_return;
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
