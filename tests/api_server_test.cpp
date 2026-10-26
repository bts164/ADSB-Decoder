// Tests for the UI API's server, as a client sees it: each test starts the server on a port of its own,
// connects over a WebSocket, and makes calls to its services, publishing what the server answers from on the
// server's channels, or putting it in the history database, and standing in for the pipeline's capture task.
// tests/wsrpc_test.cpp covers the framing itself. Last, the history database's refusal of a file from another
// version of adsb.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <absl/log/initialize.h>
#include <coro/coro.h>
#include <coro/io/ws_stream.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/sleep.h>
#include <coro/sync/timeout.h>
#include <sqlite3.h>

#include "adsb/api/v1/api.wsrpc.h"
#include "output/api_server.h"
#include "wsrpc/v1/wsrpc.pb.h"

namespace {

using namespace std::chrono_literals;
using wsrpc::v1::ClientFrame;
using wsrpc::v1::Error;
using wsrpc::v1::ServerFrame;

int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

// A watch's interval unless its request says otherwise.
constexpr auto kDefaultInterval = 100ms;

// The capture directory the live tests' server starts with. Nothing is written there: the test stands in for
// the capture task.
constexpr std::string_view kCaptureDir = "/nonexistent/captures";

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

// A call's only frame: `request` to `service`'s `rpc`.
ClientFrame call(uint32_t id, std::string_view service, std::string_view rpc,
                 const google::protobuf::Message& request) {
    ClientFrame frame;
    frame.set_id(id);
    frame.set_method("/" + std::string(service) + "/" + std::string(rpc));
    frame.set_request(request.SerializeAsString());
    frame.set_end(true);
    return frame;
}

ClientFrame receiver(uint32_t id, std::string_view rpc, const google::protobuf::Message& request) {
    return call(id, api::Receiver::kName, rpc, request);
}

ClientFrame history(uint32_t id, std::string_view rpc, const google::protobuf::Message& request) {
    return call(id, api::History::kName, rpc, request);
}

ClientFrame cancel(uint32_t id) {
    ClientFrame frame;
    frame.set_id(id);
    frame.set_cancel(true);
    return frame;
}

ClientFrame update_settings(uint32_t id, const std::string& dir) {
    api::UpdateSettingsRequest r;
    r.set_capture_dir(dir);
    return receiver(id, "UpdateSettings", r);
}

ClientFrame record_capture(uint32_t id, api::CapturePoint point, double duration_s, const std::string& name) {
    api::RecordCaptureRequest r;
    r.set_point(point);
    r.set_duration_s(duration_s);
    r.set_name(name);
    return receiver(id, "RecordCapture", r);
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

// A frame's reply, if it's of call `id` and has one.
template<typename Reply>
std::optional<Reply> reply_of(const std::optional<ServerFrame>& frame, uint32_t id) {
    Reply r;
    if (!frame || frame->id() != id || !frame->has_reply() || !r.ParseFromString(frame->reply())) return std::nullopt;
    return r;
}

bool is_end(const std::optional<ServerFrame>& frame, uint32_t id) {
    return frame && frame->id() == id && frame->end() && !frame->has_error();
}

bool is_error(const std::optional<ServerFrame>& frame, uint32_t id, Error::Code code) {
    return frame && frame->id() == id && frame->end() && frame->has_error() && frame->error().code() == code;
}

// A unary call's reply, which ends it.
template<typename Reply>
coro::Coro<std::optional<Reply>> unary(coro::WsStream& ws, const ClientFrame& frame) {
    co_await send(ws, frame);
    auto answer = co_await receive(ws);
    if (!answer || !answer->end()) co_return std::nullopt;
    co_return reply_of<Reply>(answer, frame.id());
}

// Whether the spectrum calls in progress come to `n` within a second: a call starts and ends in a task of
// its own, so the count follows the frames that start and end it a little later.
coro::Coro<bool> viewers_reach(const ApiSenders& senders, int n) {
    for (int i = 0; i < 50; i++) {
        if (senders.demand.get().spectrum_viewers == n) co_return true;
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

// What the server serves in a run().
struct Served {
    bool live = true;                                    // Receiver, with the channels' ends in a test's senders
    std::string capture_dir = std::string(kCaptureDir);  // the settings' to start with
    std::optional<std::vector<api::Aircraft>> history;   // History, from an in-memory database holding these
};

// Starts a server on `port`, serving what `served` says, and runs `test` as its client once it has said hello
// with both services. The runtime's shutdown stops the server.
template<typename Test>
void run(uint16_t port, Test test, Served served = {}) {
    coro::Runtime(2).block_on([](uint16_t port, Test test, Served served) -> coro::Coro<void> {
        api::Settings settings;
        settings.set_capture_dir(served.capture_dir);
        auto [senders, receivers] = api_channels(std::move(settings));
        std::optional<AircraftHistory> db;
        if (served.history) {
            db = AircraftHistory::open(":memory:");
            CHECK(db);
            // Queued ahead of every query, which the server sends on clones of its own.
            if (db) co_await db->insert(std::move(*served.history));
        }
        std::optional<ApiReceivers> live;
        if (served.live) live = std::move(receivers);
        api_server_spawn(port, kDefaultInterval, std::move(live), "file", std::move(db));
        coro::WsStream ws = co_await connect(port);
        co_await send(ws, hello({api::Receiver::kName, api::History::kName}));
        auto answer = co_await receive(ws);
        CHECK(answer && answer->id() == 0 && answer->has_hello());
        co_await test(ws, senders);
    }(port, test, std::move(served)));
}

// The hello's answer names the services the server has: Receiver with a pipeline, History with a database.
void test_hello() {
    for (bool live : {true, false}) {
        coro::Runtime(2).block_on([](bool live) -> coro::Coro<void> {
            auto [senders, receivers] = api_channels(api::Settings{});
            std::optional<ApiReceivers> rx;
            if (live) rx = std::move(receivers);
            const uint16_t port = live ? 18701 : 18702;
            api_server_spawn(port, kDefaultInterval, std::move(rx), "file", AircraftHistory::open(":memory:"));
            coro::WsStream ws = co_await connect(port);
            co_await send(ws, hello({api::Receiver::kName, api::History::kName}));
            auto answer = co_await receive(ws);
            CHECK(answer && answer->has_hello() && answer->hello().services_size() == (live ? 2 : 1));
            CHECK(answer && answer->hello().services(answer->hello().services_size() - 1) == api::History::kName);
            if (!live) {
                co_await send(ws, receiver(1, "GetReceiverInfo", api::GetReceiverInfoRequest{}));
                CHECK(is_error(co_await receive(ws), 1, Error::UNIMPLEMENTED));
            }
        }(live));
    }
}

void test_receiver_info() {
    run(18703, [](coro::WsStream& ws, ApiSenders&) -> coro::Coro<void> {
        auto info =
            co_await unary<api::ReceiverInfo>(ws, receiver(1, "GetReceiverInfo", api::GetReceiverInfoRequest{}));
        CHECK(info && info->source() == "file" && info->default_interval_s() == 0.1);
        CHECK(info && info->max_capture_s() == CaptureCall::kMaxDurationS);
    });
}

// WatchSettings sends the settings now, and again each time a client changes them, and the pipeline sees each
// change. ListCaptures lists the directory they name.
void test_settings() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "api_server_test_captures";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    // 10 samples of cf32_le at 1 kHz.
    std::ofstream(dir / "a.sigmf-data", std::ios::binary) << std::string(80, '\0');
    std::ofstream(dir / "a.sigmf-meta") << R"({"global": {"core:datatype": "cf32_le", "core:sample_rate": 1000},
                                             "captures": [{"core:sample_start": 0, "core:frequency": 1.09e9}]})";
    std::ofstream(dir / "ignored.txt") << "not a capture";
    run(
        18704,
        [dir](coro::WsStream& ws, ApiSenders& senders) -> coro::Coro<void> {
            co_await send(ws, receiver(1, "WatchSettings", api::WatchSettingsRequest{}));
            auto first = reply_of<api::Settings>(co_await receive(ws), 1);
            CHECK(first && first->capture_dir().empty());

            co_await send(ws, receiver(2, "ListCaptures", api::ListCapturesRequest{}));
            CHECK(is_error(co_await receive(ws), 2, Error::FAILED_PRECONDITION));
            co_await send(ws, update_settings(3, (dir / "missing").string()));
            CHECK(is_error(co_await receive(ws), 3, Error::INVALID_ARGUMENT));

            // The reply comes from the call's task, and the watch's from its own, so either can be first.
            co_await send(ws, update_settings(4, dir.string()));
            bool replied = false, watched = false;
            for (int i = 0; i < 2; i++) {
                auto frame = co_await receive(ws);
                auto reply = reply_of<api::Settings>(frame, 4);
                auto watch = reply_of<api::Settings>(frame, 1);
                if (reply && is_end(frame, 4)) replied = reply->capture_dir() == dir.string();
                if (watch && !frame->end()) watched = watch->capture_dir() == dir.string();
            }
            CHECK(replied && watched);
            CHECK(senders.settings.get().capture_dir() == dir.string());
            // Setting the directory it already has changes nothing, so the watch sends nothing.
            auto same = co_await unary<api::Settings>(ws, update_settings(5, dir.string()));
            CHECK(same && same->capture_dir() == dir.string());
            CHECK(!co_await receive(ws, 100ms));

            auto listed = co_await unary<api::ListCapturesResponse>(
                ws, receiver(6, "ListCaptures", api::ListCapturesRequest{}));
            CHECK(listed && listed->dir() == dir.string() && listed->captures_size() == 1);
            if (listed && listed->captures_size() == 1) {
                const api::Capture& c = listed->captures(0);
                CHECK(c.name() == "a" && c.data_bytes() == 80 && c.data_path() == (dir / "a.sigmf-data").string());
                CHECK(c.has_meta() && c.meta().datatype() == "cf32_le" && c.meta().rate_hz() == 1000);
                CHECK(c.meta().has_freq_hz() && c.meta().freq_hz() == 1.09e9);
                CHECK(std::abs(c.meta().duration_s() - 0.01) < 1e-9);
            }

            co_await send(ws, update_settings(7, ""));
            for (int i = 0; i < 2; i++) (void)co_await receive(ws);
            CHECK(senders.settings.get().capture_dir().empty());
            co_await send(ws, receiver(8, "ListCaptures", api::ListCapturesRequest{}));
            CHECK(is_error(co_await receive(ws), 8, Error::FAILED_PRECONDITION));
        },
        {.capture_dir = "", .history = std::nullopt});
    std::filesystem::remove_all(dir);
}

// A batch carries the frames published since the last, and a watch with nothing published sends nothing.
void test_frames() {
    run(18705, [](coro::WsStream& ws, ApiSenders& senders) -> coro::Coro<void> {
        co_await send(ws, receiver(1, "WatchFrames", api::WatchFramesRequest{}));
        CHECK(!co_await receive(ws, 3 * kDefaultInterval));
        (void)senders.frames.send(std::make_shared<const std::vector<api::Frame>>(2));
        (void)senders.frames.send(std::make_shared<const std::vector<api::Frame>>(1));
        int seen = 0;
        while (seen < 3) {
            auto batch = reply_of<api::FrameBatch>(co_await receive(ws), 1);
            CHECK(batch && !batch->frames_dropped());
            if (!batch) co_return;
            seen += batch->frames_size();
        }
        CHECK(seen == 3);

        api::WatchFramesRequest slow;
        slow.set_interval_s(1e6);
        co_await send(ws, receiver(2, "WatchFrames", slow));
        CHECK(is_error(co_await receive(ws), 2, Error::INVALID_ARGUMENT));
    });
}

// The first reply has every aircraft, even none; each later one has those that changed. A cancelled watch sends
// nothing more, though what it would send is still being published.
void test_aircraft() {
    run(18706, [](coro::WsStream& ws, ApiSenders& senders) -> coro::Coro<void> {
        co_await send(ws, receiver(1, "WatchAircraft", api::WatchAircraftRequest{}));
        auto first = reply_of<api::AircraftChanges>(co_await receive(ws), 1);
        CHECK(first && first->aircraft_size() == 0);

        (void)senders.aircraft.send(LiveAircraft{{0xabc, aircraft(0xabc, 1000)}});
        auto changes = reply_of<api::AircraftChanges>(co_await receive(ws), 1);
        CHECK(changes && changes->aircraft_size() == 1 && changes->aircraft(0).icao() == 0xabc);

        // Published again unchanged, and with one more: only the new one is sent.
        (void)senders.aircraft.send(LiveAircraft{{0xabc, aircraft(0xabc, 1000)}, {0xdef, aircraft(0xdef, 2000)}});
        changes = reply_of<api::AircraftChanges>(co_await receive(ws), 1);
        CHECK(changes && changes->aircraft_size() == 1 && changes->aircraft(0).icao() == 0xdef);

        co_await send(ws, cancel(1));
        CHECK(is_error(co_await receive(ws), 1, Error::CANCELLED));
        for (int i = 0; i < 3; i++) {
            (void)senders.aircraft.send(LiveAircraft{{0xabc, aircraft(0xabc, 2000 + i)}});
            CHECK(!co_await receive(ws, 3 * kDefaultInterval));
        }
    });
}

// Nothing until the input reports its status, then each report.
void test_input_status() {
    run(18707, [](coro::WsStream& ws, ApiSenders& senders) -> coro::Coro<void> {
        co_await send(ws, receiver(1, "WatchInputStatus", api::WatchInputStatusRequest{}));
        CHECK(!co_await receive(ws, 100ms));
        api::InputStatus status;
        status.set_ok(true);
        status.set_summary("fine");
        (void)senders.input_status.send(status);
        auto reply = reply_of<api::InputStatus>(co_await receive(ws), 1);
        CHECK(reply && reply->ok() && reply->summary() == "fine");
    });
}

// Each spectrum watch sends every row published while it's in progress. It counts as a viewer until it ends,
// by a cancel or by its connection closing.
void test_spectrum() {
    run(18708, [](coro::WsStream& ws, ApiSenders& senders) -> coro::Coro<void> {
        CHECK(senders.demand.get().spectrum_viewers == 0);
        co_await send(ws, receiver(1, "WatchSpectrum", api::WatchSpectrumRequest{}));
        co_await send(ws, receiver(2, "WatchSpectrum", api::WatchSpectrumRequest{}));
        CHECK(co_await viewers_reach(senders, 2));

        auto row = std::make_shared<api::SpectrumRow>();
        row->set_freq_hz(1.09e9);
        (void)senders.spectrum.send(std::move(row));
        unsigned ids = 0;  // a bit for each call that sent the row
        for (int i = 0; i < 2; i++) {
            auto frame = co_await receive(ws);
            for (uint32_t id : {1u, 2u}) {
                auto r = reply_of<api::SpectrumRow>(frame, id);
                if (r && r->freq_hz() == 1.09e9) ids |= 1u << id;
            }
        }
        CHECK(ids == 0b110);

        co_await send(ws, cancel(1));
        CHECK(is_error(co_await receive(ws), 1, Error::CANCELLED));
        CHECK(co_await viewers_reach(senders, 1));
        ws = co_await connect(18708);  // closes the first connection, and its call
        CHECK(co_await viewers_reach(senders, 0));
    });
}

// The server checks a capture's request, hands it to the pipeline, and relays the pipeline's reports until the
// last, or its error. A cancel closes the reports' channel.
void test_capture() {
    run(18709, [](coro::WsStream& ws, ApiSenders& senders) -> coro::Coro<void> {
        co_await send(ws, record_capture(1, api::CAPTURE_POINT_UNSPECIFIED, 1, "a"));
        CHECK(is_error(co_await receive(ws), 1, Error::INVALID_ARGUMENT));
        co_await send(ws, record_capture(2, api::INPUT, 0, "a"));
        CHECK(is_error(co_await receive(ws), 2, Error::INVALID_ARGUMENT));
        co_await send(ws, record_capture(3, api::INPUT, CaptureCall::kMaxDurationS + 1, "a"));
        CHECK(is_error(co_await receive(ws), 3, Error::INVALID_ARGUMENT));
        uint32_t id = 4;
        for (const char* name : {"", ".a", "x/a"}) {
            co_await send(ws, record_capture(id, api::INPUT, 1, name));
            CHECK(is_error(co_await receive(ws), id, Error::INVALID_ARGUMENT));
            id++;
        }

        co_await send(ws, record_capture(10, api::ENVELOPE, 1, "a"));
        auto c = co_await senders.captures.recv();
        CHECK(c && c->request.point() == api::ENVELOPE && c->request.name() == "a" && c->dir == kCaptureDir);
        if (!c) co_return;
        api::RecordCaptureProgress progress;
        progress.set_samples(10);
        CHECK(co_await c->progress.send(progress));
        progress.set_samples(20);
        progress.mutable_capture()->set_name("a");
        CHECK(co_await c->progress.send(progress));
        auto reply = reply_of<api::RecordCaptureProgress>(co_await receive(ws), 10);
        CHECK(reply && reply->samples() == 10 && !reply->has_capture());
        reply = reply_of<api::RecordCaptureProgress>(co_await receive(ws), 10);
        CHECK(reply && reply->samples() == 20 && reply->capture().name() == "a");
        CHECK(is_end(co_await receive(ws), 10));

        // The pipeline's error is the call's.
        co_await send(ws, record_capture(11, api::INPUT, 1, "b"));
        c = co_await senders.captures.recv();
        CHECK(c);
        if (!c) co_return;
        CHECK(co_await c->progress.send(std::unexpected(wsrpc::CallError(Error::FAILED_PRECONDITION, "b exists"))));
        CHECK(is_error(co_await receive(ws), 11, Error::FAILED_PRECONDITION));

        co_await send(ws, record_capture(12, api::INPUT, 1, "c"));
        c = co_await senders.captures.recv();
        CHECK(c);
        if (!c) co_return;
        co_await send(ws, cancel(12));
        CHECK(is_error(co_await receive(ws), 12, Error::CANCELLED));
        bool closed = false;
        for (int i = 0; i < 50 && !closed; i++) {
            closed = c->progress.is_closed();
            if (!closed) co_await coro::sleep_for(20ms);
        }
        CHECK(closed);
    });
}

// With captures off, the capture calls are refused, as is a call to a service that isn't served.
void test_captures_off() {
    run(
        18710,
        [](coro::WsStream& ws, ApiSenders&) -> coro::Coro<void> {
            co_await send(ws, record_capture(1, api::INPUT, 1, "a"));
            CHECK(is_error(co_await receive(ws), 1, Error::FAILED_PRECONDITION));
            co_await send(ws, history(2, "GetSnapshot", api::GetSnapshotRequest{}));
            CHECK(is_error(co_await receive(ws), 2, Error::UNIMPLEMENTED));
        },
        {.capture_dir = "", .history = std::nullopt});
}

void test_heatmap_area() {
    run(
        18711,
        [](coro::WsStream& ws, ApiSenders&) -> coro::Coro<void> {
            api::GetHeatmapRequest area;
            area.set_west(10);
            area.set_east(10);
            area.set_south(50);
            area.set_north(51);
            co_await send(ws, history(1, "GetHeatmap", area));
            CHECK(is_error(co_await receive(ws), 1, Error::INVALID_ARGUMENT));
        },
        {.live = false, .history = std::vector<api::Aircraft>{}});
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
            auto snap = co_await unary<api::Snapshot>(ws, history(1, "GetSnapshot", api::GetSnapshotRequest{}));
            CHECK(snap);
            if (snap) {
                CHECK(snap->t() == 1010 && snap->t_min() == 1000 && snap->t_max() == 1010);
                CHECK(snap->aircraft_size() == 2);
                for (const api::Snapshot::Entry& e : snap->aircraft()) {
                    if (e.state().icao() == 0xabc) {
                        CHECK(e.state().altitude_ft() == 1100);
                        CHECK(e.track_size() == 2 && e.track(0).altitude_ft() == 1000 && e.track(1).lat() == 50.1);
                    } else {
                        CHECK(e.state().icao() == 0xdef && e.track_size() == 0);
                    }
                }
            }

            // Before the second aircraft was heard from.
            api::GetSnapshotRequest earlier;
            earlier.set_t(1003);
            snap = co_await unary<api::Snapshot>(ws, history(2, "GetSnapshot", earlier));
            CHECK(snap && snap->aircraft_size() == 1 && snap->aircraft(0).state().altitude_ft() == 1000);

            api::GetAircraftHistoryRequest one;
            one.set_icao(0xabc);
            auto h = co_await unary<api::AircraftHistory>(ws, history(3, "GetAircraftHistory", one));
            CHECK(h);
            if (h) {
                CHECK(h->t() == 1010 && h->points_size() == 2);
                CHECK(h->points_size() == 2 && h->points(0).t() == 1000 && h->points(0).altitude_ft() == 1000);
                CHECK(h->points_size() == 2 && h->points(0).ground_speed_kt() == 300);
                CHECK(h->points_size() == 2 && !h->points(1).has_ground_speed_kt() && !h->points(1).has_signal_db());
            }

            // Every update falls in the minute from 960 s.
            auto activity = co_await unary<api::Activity>(ws, history(4, "GetActivity", api::GetActivityRequest{}));
            CHECK(activity && activity->bucket_s() == 60 && activity->t0() == 960);
            CHECK(activity && activity->counts_size() == 1 && activity->counts(0) == 2);

            // One flight, so no cell counts more than one.
            api::GetHeatmapRequest area;
            area.set_west(9);
            area.set_east(11);
            area.set_south(49);
            area.set_north(51);
            area.set_width(8);
            area.set_height(8);
            auto map = co_await unary<api::Heatmap>(ws, history(5, "GetHeatmap", area));
            CHECK(map && map->max() == 1 && map->cell_index_size() >= 1);
            CHECK(map && map->cell_index_size() == map->cell_count_size());
        },
        {.live = false, .history = std::move(rows)});
}

// A client that disconnects with calls in progress doesn't disturb the next.
void test_disconnect() {
    run(18713, [](coro::WsStream& ws, ApiSenders& senders) -> coro::Coro<void> {
        co_await send(ws, receiver(1, "WatchAircraft", api::WatchAircraftRequest{}));
        co_await send(ws, receiver(2, "WatchFrames", api::WatchFramesRequest{}));
        CHECK(co_await receive(ws));
        ws = co_await connect(18713);  // closes the first connection
        for (int i = 0; i < 3; i++) {
            (void)senders.aircraft.send(LiveAircraft{{0xabc, aircraft(0xabc, 2000 + i)}});
            (void)senders.frames.send(std::make_shared<const std::vector<api::Frame>>(1));
            co_await coro::sleep_for(kDefaultInterval);
        }
        co_await send(ws, hello({api::Receiver::kName}));
        CHECK(co_await receive(ws));
        auto info =
            co_await unary<api::ReceiverInfo>(ws, receiver(1, "GetReceiverInfo", api::GetReceiverInfoRequest{}));
        CHECK(info && info->source() == "file");
    });
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
    test_hello();
    test_receiver_info();
    test_settings();
    test_frames();
    test_aircraft();
    test_input_status();
    test_spectrum();
    test_capture();
    test_captures_off();
    test_heatmap_area();
    test_history();
    test_disconnect();
    test_old_database();
    std::printf(g_failures ? "%d FAILURE(S)\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
