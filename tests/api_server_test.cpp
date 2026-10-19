// Tests for the UI API's server, as a client sees it: each test starts the server on a port of its own,
// connects over a WebSocket, and publishes what the server answers from on the server's channels, or puts it
// in the history database. Last, the history database's refusal of a file from another version of adsb.

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <absl/log/initialize.h>
#include <coro/coro.h>
#include <coro/io/ws_stream.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/sleep.h>
#include <coro/sync/timeout.h>
#include <sqlite3.h>

#include "output/api_server.h"

namespace {

using namespace std::chrono_literals;

int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

// The server's cap on one connection's calls in progress.
constexpr uint32_t kClientCalls = 8;

// A subscription's interval unless its request says otherwise.
constexpr auto kDefaultInterval = 100ms;

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

coro::Coro<void> send(coro::WsStream& ws, const api::ClientMessage& msg) {
    const std::string s = msg.SerializeAsString();
    co_await ws.send(std::as_bytes(std::span(s)));
}

api::ClientMessage server_info(uint32_t id) {
    api::ClientMessage msg;
    msg.set_id(id);
    msg.mutable_server_info();
    return msg;
}

api::ClientMessage subscribe(uint32_t id) {
    api::ClientMessage msg;
    msg.set_id(id);
    msg.mutable_subscribe();
    return msg;
}

api::ClientMessage spectrum(uint32_t id) {
    api::ClientMessage msg;
    msg.set_id(id);
    msg.mutable_spectrum();
    return msg;
}

api::ClientMessage cancel(uint32_t id) {
    api::ClientMessage msg;
    msg.set_id(id);
    msg.mutable_cancel();
    return msg;
}

// The next message from the server, or none within `wait`.
coro::Coro<std::optional<api::ServerMessage>> receive(coro::WsStream& ws, std::chrono::nanoseconds wait = 2s) {
    auto result = co_await coro::timeout(wait, ws.receive());
    if (result.index() != 0) co_return std::nullopt;
    const coro::WsStream::Message& bytes = std::get<0>(result).value;
    api::ServerMessage msg;
    if (bytes.is_text || !msg.ParseFromArray(bytes.data.data(), static_cast<int>(bytes.data.size())))
        co_return std::nullopt;
    co_return msg;
}

bool is_error(const std::optional<api::ServerMessage>& msg, uint32_t id, api::Error::Code code) {
    return msg && msg->id() == id && msg->end() && msg->has_error() && msg->error().code() == code;
}

// Whether the spectrum calls in progress come to `n` within a second: a call starts and ends in a task of
// its own, so the count follows the messages that start and end it a little later.
coro::Coro<bool> viewers_reach(const ApiSenders& senders, int n) {
    for (int i = 0; i < 50; i++) {
        if (senders.settings.get().spectrum_viewers == n) co_return true;
        co_await coro::sleep_for(20ms);
    }
    co_return false;
}

api::Aircraft aircraft(uint32_t icao, int32_t altitude_ft) {
    api::Aircraft ac;
    ac.set_icao(icao);
    ac.set_altitude_ft(altitude_ft);
    return ac;
}

// An aircraft as recorded at `t`, at (`lat`, `lon`) unless it's NaN.
api::Aircraft recorded(uint32_t icao, double t, int32_t altitude_ft, double lat = NAN, double lon = NAN) {
    api::Aircraft ac = aircraft(icao, altitude_ft);
    ac.set_last_seen_unix_s(t);
    if (!std::isnan(lat)) {
        ac.set_lat(lat);
        ac.set_lon(lon);
    }
    return ac;
}

// Starts a server on `port`, with an in-memory history database holding `history` if there is one, and runs
// `test` as its client. Without `live`, the server runs as it does with no pipeline. The runtime's shutdown
// stops the server.
template <typename Test>
void run(uint16_t port, Test test, std::optional<std::vector<api::Aircraft>> history = std::nullopt,
         bool live = true) {
    coro::Runtime(2).block_on(
        [](uint16_t port, Test test, std::optional<std::vector<api::Aircraft>> history,
           bool live) -> coro::Coro<void> {
            auto [senders, receivers] = api_channels();
            std::optional<AircraftHistory> db;
            if (history) {
                db = AircraftHistory::open(":memory:");
                CHECK(db);
                // Queued ahead of every query, which the server sends on clones of its own.
                if (db) co_await db->insert(std::move(*history));
            }
            api_server_spawn(port, kDefaultInterval, live, std::move(receivers), std::move(db));
            coro::WsStream ws = co_await connect(port);
            co_await test(ws, senders);
        }(port, test, std::move(history), live));
}

void test_unary() {
    run(18701, [](coro::WsStream& ws, ApiSenders&) -> coro::Coro<void> {
        co_await send(ws, server_info(1));
        auto reply = co_await receive(ws);
        CHECK(reply && reply->id() == 1 && reply->end() && reply->has_server_info());
        CHECK(reply && !reply->server_info().history() && reply->server_info().spectrum());
        CHECK(reply && reply->server_info().default_interval_s() == 0.1);
    });
}

// A subscription's updates carry what was published since the last one, and each aircraft only when it
// has changed.
void test_stream() {
    run(18702, [](coro::WsStream& ws, ApiSenders& senders) -> coro::Coro<void> {
        co_await send(ws, subscribe(1));
        (void)senders.aircraft.send(LiveAircraft{{0xabc, aircraft(0xabc, 1000)}});
        auto frames = std::make_shared<std::vector<api::Frame>>(1);
        (void)senders.live.send(LiveItem(std::shared_ptr<const std::vector<api::Frame>>(frames)));
        int aircraft_seen = 0, frames_seen = 0;
        while (aircraft_seen == 0 || frames_seen == 0) {
            auto msg = co_await receive(ws);
            CHECK(msg && msg->id() == 1 && !msg->end() && msg->has_update());
            if (!msg) co_return;
            aircraft_seen += msg->update().aircraft_size();
            frames_seen += msg->update().frames_size();
        }
        CHECK(aircraft_seen == 1 && frames_seen == 1);

        // Published again unchanged, and with one more: only the new one is sent.
        (void)senders.aircraft.send(LiveAircraft{{0xabc, aircraft(0xabc, 1000)}, {0xdef, aircraft(0xdef, 2000)}});
        auto msg = co_await receive(ws);
        CHECK(msg && msg->update().aircraft_size() == 1 && msg->update().aircraft(0).icao() == 0xdef);
    });
}

void test_errors() {
    run(18703, [](coro::WsStream& ws, ApiSenders&) -> coro::Coro<void> {
        api::ClientMessage snapshot;
        snapshot.set_id(1);
        snapshot.mutable_snapshot();
        co_await send(ws, snapshot);
        CHECK(is_error(co_await receive(ws), 1, api::Error::NO_HISTORY));

        api::ClientMessage slow = subscribe(2);
        slow.mutable_subscribe()->set_interval_s(1e6);
        co_await send(ws, slow);
        CHECK(is_error(co_await receive(ws), 2, api::Error::INVALID_ARGUMENT));
    });
    run(
        18704,
        [](coro::WsStream& ws, ApiSenders&) -> coro::Coro<void> {
            api::ClientMessage heatmap;
            heatmap.set_id(1);
            api::HeatmapRequest& area = *heatmap.mutable_heatmap();
            area.set_west(10);
            area.set_east(10);
            area.set_south(50);
            area.set_north(51);
            co_await send(ws, heatmap);
            CHECK(is_error(co_await receive(ws), 1, api::Error::INVALID_ARGUMENT));
        },
        std::vector<api::Aircraft>{});
}

// Each history call, against a database holding two updates of one aircraft with a position, and one of
// another without.
void test_history() {
    std::vector<api::Aircraft> rows = {
        recorded(0xabc, 1000, 1000, 50.0, 10.0),
        recorded(0xdef, 1005, 2000),
        recorded(0xabc, 1010, 1100, 50.1, 10.1),
    };
    rows[0].set_ground_speed_kt(300);
    run(
        18712,
        [](coro::WsStream& ws, ApiSenders&) -> coro::Coro<void> {
            api::ClientMessage msg;
            msg.set_id(1);
            msg.mutable_snapshot();
            co_await send(ws, msg);
            auto reply = co_await receive(ws);
            CHECK(reply && reply->id() == 1 && reply->end() && reply->has_snapshot());
            if (reply && reply->has_snapshot()) {
                const api::Snapshot& snap = reply->snapshot();
                CHECK(snap.t() == 1010 && snap.t_min() == 1000 && snap.t_max() == 1010);
                CHECK(snap.aircraft_size() == 2);
                for (const api::Snapshot::Entry& e : snap.aircraft()) {
                    if (e.state().icao() == 0xabc) {
                        CHECK(e.state().altitude_ft() == 1100);
                        CHECK(e.track_size() == 2 && e.track(0).altitude_ft() == 1000 && e.track(1).lat() == 50.1);
                    } else {
                        CHECK(e.state().icao() == 0xdef && e.track_size() == 0);
                    }
                }
            }

            // Before the second aircraft was heard from.
            msg.set_id(2);
            msg.mutable_snapshot()->set_t(1003);
            co_await send(ws, msg);
            reply = co_await receive(ws);
            CHECK(reply && reply->id() == 2 && reply->snapshot().aircraft_size() == 1);
            CHECK(reply && reply->snapshot().aircraft(0).state().altitude_ft() == 1000);

            msg.set_id(3);
            msg.mutable_aircraft_history()->set_icao(0xabc);
            co_await send(ws, msg);
            reply = co_await receive(ws);
            CHECK(reply && reply->id() == 3 && reply->has_aircraft_history());
            if (reply && reply->has_aircraft_history()) {
                const api::AircraftHistory& h = reply->aircraft_history();
                CHECK(h.t() == 1010 && h.points_size() == 2);
                CHECK(h.points_size() == 2 && h.points(0).t() == 1000 && h.points(0).altitude_ft() == 1000);
                CHECK(h.points_size() == 2 && h.points(0).ground_speed_kt() == 300);
                CHECK(h.points_size() == 2 && !h.points(1).has_ground_speed_kt() && !h.points(1).has_signal_db());
            }

            // Every update falls in the minute from 960 s.
            msg.set_id(4);
            msg.mutable_activity();
            co_await send(ws, msg);
            reply = co_await receive(ws);
            CHECK(reply && reply->id() == 4 && reply->has_activity());
            CHECK(reply && reply->activity().bucket_s() == 60 && reply->activity().t0() == 960);
            CHECK(reply && reply->activity().counts_size() == 1 && reply->activity().counts(0) == 2);

            // One flight, so no cell counts more than one.
            msg.set_id(5);
            api::HeatmapRequest& area = *msg.mutable_heatmap();
            area.set_west(9);
            area.set_east(11);
            area.set_south(49);
            area.set_north(51);
            area.set_width(8);
            area.set_height(8);
            co_await send(ws, msg);
            reply = co_await receive(ws);
            CHECK(reply && reply->id() == 5 && reply->has_heatmap());
            if (reply && reply->has_heatmap()) {
                const api::Heatmap& map = reply->heatmap();
                CHECK(map.max() == 1 && map.cell_index_size() >= 1 && map.cell_index_size() == map.cell_count_size());
            }
        },
        std::move(rows));
}

// A cancelled subscription sends nothing more, though what it would send is still being published.
void test_cancel() {
    run(18705, [](coro::WsStream& ws, ApiSenders& senders) -> coro::Coro<void> {
        co_await send(ws, subscribe(1));
        (void)senders.aircraft.send(LiveAircraft{{0xabc, aircraft(0xabc, 1000)}});
        auto update = co_await receive(ws);
        CHECK(update && update->id() == 1 && update->has_update());

        co_await send(ws, cancel(1));
        CHECK(is_error(co_await receive(ws), 1, api::Error::CANCELLED));
        for (int i = 0; i < 3; i++) {
            (void)senders.aircraft.send(LiveAircraft{{0xabc, aircraft(0xabc, 2000 + i)}});
            CHECK(!co_await receive(ws, 3 * kDefaultInterval));
        }
    });
}

void test_cancel_after_end() {
    run(18706, [](coro::WsStream& ws, ApiSenders&) -> coro::Coro<void> {
        co_await send(ws, server_info(1));
        auto reply = co_await receive(ws);
        CHECK(reply && reply->id() == 1 && reply->end() && !reply->has_error());
        co_await send(ws, cancel(1));
        CHECK(!co_await receive(ws, 100ms));
    });
}

void test_call_cap() {
    run(18707, [](coro::WsStream& ws, ApiSenders&) -> coro::Coro<void> {
        // Nothing is published, so the subscriptions stay in progress and send nothing.
        for (uint32_t id = 1; id <= kClientCalls; id++) co_await send(ws, subscribe(id));
        co_await send(ws, server_info(kClientCalls + 1));
        CHECK(is_error(co_await receive(ws), kClientCalls + 1, api::Error::RESOURCE_EXHAUSTED));
        // A cancel isn't a call, so the cap doesn't refuse it, and a cancelled call frees its place.
        co_await send(ws, cancel(3));
        CHECK(is_error(co_await receive(ws), 3, api::Error::CANCELLED));
        co_await coro::sleep_for(50ms);  // for the server to hear the call has finished
        co_await send(ws, server_info(kClientCalls + 2));
        auto reply = co_await receive(ws);
        CHECK(reply && reply->id() == kClientCalls + 2 && reply->has_server_info());
    });
}

void test_repeated_id() {
    run(18708, [](coro::WsStream& ws, ApiSenders&) -> coro::Coro<void> {
        co_await send(ws, server_info(5));
        co_await send(ws, server_info(5));
        // The refusal comes from the connection's reader, and the reply from the call's task, so either can
        // be first.
        int replies = 0, refusals = 0;
        for (int i = 0; i < 2; i++) {
            auto msg = co_await receive(ws);
            if (msg && msg->id() == 5 && msg->has_server_info()) replies++;
            if (is_error(msg, 5, api::Error::INVALID_ARGUMENT)) refusals++;
        }
        CHECK(replies == 1 && refusals == 1);
    });
}

void test_unknown_call() {
    run(18709, [](coro::WsStream& ws, ApiSenders&) -> coro::Coro<void> {
        api::ClientMessage msg;
        msg.set_id(1);
        co_await send(ws, msg);
        CHECK(is_error(co_await receive(ws), 1, api::Error::UNIMPLEMENTED));
    });
}

// A message that isn't a ClientMessage, or isn't binary, is ignored, and the connection carries on.
void test_garbage() {
    run(18710, [](coro::WsStream& ws, ApiSenders&) -> coro::Coro<void> {
        const std::byte garbage[] = {std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
        co_await ws.send(garbage);
        co_await ws.send(std::string_view("hello"));
        CHECK(!co_await receive(ws, 100ms));
        co_await send(ws, server_info(1));
        auto reply = co_await receive(ws);
        CHECK(reply && reply->id() == 1 && reply->has_server_info());
    });
}

// A client that disconnects with calls in progress doesn't disturb the next.
void test_disconnect() {
    run(18711, [](coro::WsStream& ws, ApiSenders& senders) -> coro::Coro<void> {
        co_await send(ws, subscribe(1));
        co_await send(ws, subscribe(2));
        (void)senders.aircraft.send(LiveAircraft{{0xabc, aircraft(0xabc, 1000)}});
        CHECK(co_await receive(ws));
        ws = co_await connect(18711);  // closes the first connection
        for (int i = 0; i < 3; i++) {
            (void)senders.aircraft.send(LiveAircraft{{0xabc, aircraft(0xabc, 2000 + i)}});
            co_await coro::sleep_for(kDefaultInterval);
        }
        co_await send(ws, server_info(1));
        auto reply = co_await receive(ws);
        CHECK(reply && reply->id() == 1 && reply->has_server_info());
    });
}

// A spectrum call sends the spectrum rows published while it's in progress, and none of the frames. It
// counts as a viewer until it ends, by a cancel or by its connection closing.
void test_spectrum() {
    run(18712, [](coro::WsStream& ws, ApiSenders& senders) -> coro::Coro<void> {
        CHECK(senders.settings.get().spectrum_viewers == 0);
        co_await send(ws, spectrum(1));
        co_await send(ws, spectrum(2));
        CHECK(co_await viewers_reach(senders, 2));

        (void)senders.live.send(LiveItem(std::make_shared<const std::vector<api::Frame>>(1)));
        (void)senders.live.send(LiveItem(std::make_shared<const api::SpectrumRow>()));
        unsigned ids = 0;  // a bit for each call that sent the row
        for (int i = 0; i < 2; i++) {
            auto msg = co_await receive(ws);
            if (msg && !msg->end() && msg->has_spectrum()) ids |= 1u << msg->id();
        }
        CHECK(ids == 0b110);
        CHECK(!co_await receive(ws, 100ms));  // the frames went to neither

        co_await send(ws, cancel(1));
        CHECK(is_error(co_await receive(ws), 1, api::Error::CANCELLED));
        CHECK(co_await viewers_reach(senders, 1));
        ws = co_await connect(18712);  // closes the first connection, and its call
        CHECK(co_await viewers_reach(senders, 0));
    });
}

// Without a pipeline, as with `serve`, the server says it has no spectrum, and refuses the call.
void test_spectrum_without_live() {
    run(
        18713,
        [](coro::WsStream& ws, ApiSenders&) -> coro::Coro<void> {
            co_await send(ws, server_info(1));
            auto reply = co_await receive(ws);
            CHECK(reply && reply->has_server_info() && !reply->server_info().spectrum());
            co_await send(ws, spectrum(2));
            CHECK(is_error(co_await receive(ws), 2, api::Error::NO_LIVE_INPUT));
        },
        std::nullopt, false);
}

// The UI API's history database opens a new file or one it wrote, and refuses one from another version of
// adsb: one from before the version was recorded, with the table and version 0, or one with a newer version.
void test_old_database() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    // Writes `sql` to a new database file named `name`, and returns its path.
    auto make = [&dir](const char* name, const char* sql) {
        const std::string path = (dir / name).string();
        std::filesystem::remove(path);
        sqlite3* db = nullptr;
        CHECK(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
        CHECK(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);
        return path;
    };
    const std::string fresh = (dir / "api_server_test_new.sqlite").string();
    std::filesystem::remove(fresh);
    const std::string v1 = make("api_server_test_v1.sqlite",
                                "CREATE TABLE aircraft_updates (t REAL NOT NULL, icao TEXT NOT NULL, lat REAL, "
                                "lon REAL, state TEXT NOT NULL);");
    const std::string v3 = make("api_server_test_v3.sqlite", "PRAGMA user_version = 3;");

    coro::Runtime(2).block_on([](std::string fresh, std::string v1, std::string v3) -> coro::Coro<void> {
        CHECK(AircraftHistory::open(fresh));
        CHECK(AircraftHistory::open(fresh));  // again, now that it's been written
        CHECK(!AircraftHistory::open(v1));
        CHECK(!AircraftHistory::open(v3));
        co_return;
    }(fresh, v1, v3));
    for (const std::string& path : {fresh, v1, v3}) {
        // WAL mode leaves these beside the new file.
        for (const char* suffix : {"", "-wal", "-shm"}) std::filesystem::remove(path + suffix);
    }
}

}  // namespace

int main() {
    absl::InitializeLog();
    test_unary();
    test_stream();
    test_errors();
    test_cancel();
    test_cancel_after_end();
    test_call_cap();
    test_repeated_id();
    test_unknown_call();
    test_garbage();
    test_disconnect();
    test_spectrum();
    test_spectrum_without_live();
    test_history();
    test_old_database();
    std::printf(g_failures ? "%d FAILURE(S)\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
