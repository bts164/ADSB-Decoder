// Tests for the UI API's server, as a client sees it: each test starts the server on a port of its own,
// connects over a WebSocket, and makes calls to its services, publishing what the server answers from on
// channels it creates as the pipeline does, or putting it in the history database. The capture tests write
// real files. tests/wsrpc_test.cpp covers the framing itself. Last, the history database's upgrade of a file
// from the version before, and its refusal of one from any other version of adsb.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
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
#include <coro/sync/broadcast.h>
#include <coro/sync/sleep.h>
#include <coro/sync/timeout.h>
#include <coro/sync/watch.h>
#include <sqlite3.h>

#include "adsb/api/v1/api.wsrpc.h"
#include "dsp/iq_block.h"
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

// The capture directory the live tests' server starts with. Only the capture tests write, each into a
// directory of its own.
constexpr std::string_view kCaptureDir = "/nonexistent/captures";

// The capture points' rates, and the input's center frequency.
constexpr double kInputRateHz = 1000;
constexpr double kEnvelopeRateHz = 2000;
constexpr double kFreqHz = 1.09e9;

// The pipeline's end of each output, which a test publishes on as the pipeline's stages do.
struct Feeds {
    coro::BroadcastSender<LiveFrames> frames;
    coro::WatchSender<LiveAircraft> aircraft;
    coro::BroadcastSender<LiveSpectrumRow> spectrum;
    coro::BroadcastSender<IqBlock> input;
    coro::BroadcastSender<EnvelopeShare> envelope;
    coro::WatchSender<std::optional<api::InputStatus>> input_status;
    coro::WatchSender<api::ReceiverPosition> receiver_position;
};

// The receiver's position in `state`, with a fix at (`lat`, `lon`) unless lat is NaN.
api::ReceiverPosition position(double t, api::GpsState state, double lat = NAN, double lon = NAN) {
    api::ReceiverPosition p;
    p.set_t(t);
    p.set_state(state);
    if (!std::isnan(lat)) {
        p.set_lat(lat);
        p.set_lon(lon);
    }
    return p;
}

// The outputs' channels, as deep as the pipeline's: the test's ends, and the server's.
std::pair<Feeds, ReceiverFeeds> make_feeds(api::Settings settings) {
    auto [frames, frames_rx] = coro::broadcast_channel<LiveFrames>(64);
    auto [aircraft, aircraft_rx] = coro::watch_channel(LiveAircraft{});
    auto [spectrum, spectrum_rx] = coro::broadcast_channel<LiveSpectrumRow>(64);
    auto [input, input_rx] = coro::broadcast_channel<IqBlock>(8);
    auto [envelope, envelope_rx] = coro::broadcast_channel<EnvelopeShare>(8);
    auto [status, status_rx] = coro::watch_channel(std::optional<api::InputStatus>{});
    auto [receiver_position, receiver_position_rx] = coro::watch_channel(position(0, api::NO_GPS));
    ReceiverFeeds served{
        .outputs =
            {
                .frames = frames.inactive(),
                .aircraft = std::move(aircraft_rx),
                .spectrum = spectrum.inactive(),
                .input = input.inactive(),
                .envelope = envelope.inactive(),
                .input_point = {"input", "capture input missing", kInputRateHz, kFreqHz},
                .envelope_point = {"envelope", "capture envelope missing", kEnvelopeRateHz, {}},
            },
        .input_status = std::move(status_rx),
        .receiver_position = std::move(receiver_position_rx),
        .settings = std::move(settings),
    };
    return {Feeds{std::move(frames), std::move(aircraft), std::move(spectrum), std::move(input),
                  std::move(envelope), std::move(status), std::move(receiver_position)},
            std::move(served)};
}

// A block of `n` zero samples of cf32, from stream index `start`.
IqBlock block(size_t n, uint64_t start) { return IqBlock(IqBuffer::zeroed<iq::F32>(IqPadding{}, n, start)); }

// An envelope run of `n` samples, from stream index `start`.
EnvelopeShare envelope_run(size_t n, uint64_t start) {
    return std::make_shared<const EnvelopeRun>(EnvelopeRun{start, std::vector<float>(n, 1.0f)});
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

// Whether `out` comes to have readers, or to have none, as `read` says, within a second: a call starts and
// ends in a task of its own, so it activates and drops its receiver a little after the frames that start and
// end it.
template<typename T>
coro::Coro<bool> read_within(const coro::BroadcastSender<T>& out, bool read) {
    for (int i = 0; i < 50; i++) {
        if (out.all_receivers_dropped() != read) co_return true;
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
    bool live = true;                                    // Receiver, with the channels' other ends in the test's
    std::string capture_dir = std::string(kCaptureDir);  // the settings' to start with
    std::optional<std::vector<api::Aircraft>> history;   // History, from an in-memory database holding these
    std::vector<api::ReceiverPosition> positions;        // and these rows of the receiver's position
};

// Starts a server on `port`, serving what `served` says, and runs `test` as its client once it has said hello
// with both services. The runtime's shutdown stops the server.
template<typename Test>
void run(uint16_t port, Test test, Served served = {}) {
    coro::Runtime(2).block_on([](uint16_t port, Test test, Served served) -> coro::Coro<void> {
        api::Settings settings;
        settings.set_capture_dir(served.capture_dir);
        auto [feeds, outputs] = make_feeds(std::move(settings));
        std::optional<AircraftHistory> db;
        if (served.history) {
            db = AircraftHistory::open(":memory:");
            CHECK(db);
            // Queued ahead of every query, which the server sends on clones of its own.
            if (db) co_await db->insert(std::move(*served.history));
            for (api::ReceiverPosition& p : served.positions)
                if (db) co_await db->insert_position(std::move(p));
        }
        std::optional<ReceiverFeeds> live;
        if (served.live) live = std::move(outputs);
        api_server_spawn(port, kDefaultInterval, std::move(live), "file", std::move(db));
        coro::WsStream ws = co_await connect(port);
        co_await send(ws, hello({api::Receiver::kName, api::History::kName}));
        auto answer = co_await receive(ws);
        CHECK(answer && answer->id() == 0 && answer->has_hello());
        co_await test(ws, feeds);
    }(port, test, std::move(served)));
}

// The hello's answer names the services the server has: Receiver with a pipeline, History with a database.
void test_hello() {
    for (bool live : {true, false}) {
        coro::Runtime(2).block_on([](bool live) -> coro::Coro<void> {
            auto [feeds, outputs] = make_feeds(api::Settings{});
            std::optional<ReceiverFeeds> rx;
            if (live) rx = std::move(outputs);
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
    run(18703, [](coro::WsStream& ws, Feeds&) -> coro::Coro<void> {
        auto info =
            co_await unary<api::ReceiverInfo>(ws, receiver(1, "GetReceiverInfo", api::GetReceiverInfoRequest{}));
        CHECK(info && info->source() == "file" && info->default_interval_s() == 0.1);
        CHECK(info && info->max_capture_s() == CapturePoint::kMaxDurationS);
    });
}

// WatchSettings sends the settings now, and again each time a client changes them. ListCaptures lists the
// directory they name.
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
        [dir](coro::WsStream& ws, Feeds&) -> coro::Coro<void> {
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
            bool off = false;
            for (int i = 0; i < 2; i++) {
                auto frame = co_await receive(ws);
                if (auto reply = reply_of<api::Settings>(frame, 7)) off = reply->capture_dir().empty();
            }
            CHECK(off);
            co_await send(ws, receiver(8, "ListCaptures", api::ListCapturesRequest{}));
            CHECK(is_error(co_await receive(ws), 8, Error::FAILED_PRECONDITION));
        },
        {.capture_dir = "", .history = std::nullopt});
    std::filesystem::remove_all(dir);
}

// A batch carries the frames published since the last, and a watch with nothing published sends nothing.
void test_frames() {
    run(18705, [](coro::WsStream& ws, Feeds& feeds) -> coro::Coro<void> {
        co_await send(ws, receiver(1, "WatchFrames", api::WatchFramesRequest{}));
        CHECK(!co_await receive(ws, 3 * kDefaultInterval));
        (void)feeds.frames.send(std::make_shared<const std::vector<api::Frame>>(2));
        (void)feeds.frames.send(std::make_shared<const std::vector<api::Frame>>(1));
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
    run(18706, [](coro::WsStream& ws, Feeds& feeds) -> coro::Coro<void> {
        co_await send(ws, receiver(1, "WatchAircraft", api::WatchAircraftRequest{}));
        auto first = reply_of<api::AircraftChanges>(co_await receive(ws), 1);
        CHECK(first && first->aircraft_size() == 0);

        (void)feeds.aircraft.send(LiveAircraft{{0xabc, aircraft(0xabc, 1000)}});
        auto changes = reply_of<api::AircraftChanges>(co_await receive(ws), 1);
        CHECK(changes && changes->aircraft_size() == 1 && changes->aircraft(0).icao() == 0xabc);

        // Published again unchanged, and with one more: only the new one is sent.
        (void)feeds.aircraft.send(LiveAircraft{{0xabc, aircraft(0xabc, 1000)}, {0xdef, aircraft(0xdef, 2000)}});
        changes = reply_of<api::AircraftChanges>(co_await receive(ws), 1);
        CHECK(changes && changes->aircraft_size() == 1 && changes->aircraft(0).icao() == 0xdef);

        co_await send(ws, cancel(1));
        CHECK(is_error(co_await receive(ws), 1, Error::CANCELLED));
        for (int i = 0; i < 3; i++) {
            (void)feeds.aircraft.send(LiveAircraft{{0xabc, aircraft(0xabc, 2000 + i)}});
            CHECK(!co_await receive(ws, 3 * kDefaultInterval));
        }
    });
}

// Nothing until the input reports its status, then each report.
void test_input_status() {
    run(18707, [](coro::WsStream& ws, Feeds& feeds) -> coro::Coro<void> {
        co_await send(ws, receiver(1, "WatchInputStatus", api::WatchInputStatusRequest{}));
        CHECK(!co_await receive(ws, 100ms));
        api::InputStatus status;
        status.set_ok(true);
        status.set_summary("fine");
        (void)feeds.input_status.send(status);
        auto reply = reply_of<api::InputStatus>(co_await receive(ws), 1);
        CHECK(reply && reply->ok() && reply->summary() == "fine");
    });
}

// The receiver's position at once, then each change.
void test_receiver_position() {
    run(18714, [](coro::WsStream& ws, Feeds& feeds) -> coro::Coro<void> {
        co_await send(ws, receiver(1, "WatchReceiverPosition", api::WatchReceiverPositionRequest{}));
        auto reply = reply_of<api::ReceiverPosition>(co_await receive(ws), 1);
        CHECK(reply && reply->state() == api::NO_GPS);
        (void)feeds.receiver_position.send(position(1000, api::FIX_3D, 50, 10));
        reply = reply_of<api::ReceiverPosition>(co_await receive(ws), 1);
        CHECK(reply && reply->state() == api::FIX_3D && reply->lat() == 50 && reply->lon() == 10);
    });
}

// Each spectrum watch sends every row published while it's in progress, through a receiver of its own that it
// drops when it ends, by a cancel or by its connection closing. Nothing reads the rows before or after.
void test_spectrum() {
    run(18708, [](coro::WsStream& ws, Feeds& feeds) -> coro::Coro<void> {
        CHECK(feeds.spectrum.all_receivers_dropped());
        co_await send(ws, receiver(1, "WatchSpectrum", api::WatchSpectrumRequest{}));
        co_await send(ws, receiver(2, "WatchSpectrum", api::WatchSpectrumRequest{}));
        // Numbered rows until one reaches both calls; each call's earlier ones come before it.
        double both = 0;
        for (int i = 1; i <= 50 && both == 0; i++) {
            auto row = std::make_shared<api::SpectrumRow>();
            row->set_freq_hz(i);
            if (feeds.spectrum.send(std::move(row)).value_or(0) == 2) both = i;
            else co_await coro::sleep_for(20ms);
        }
        CHECK(both != 0);
        unsigned ids = 0;  // a bit for each call that sent the row both had
        for (int i = 0; i < 100 && ids != 0b110; i++) {
            auto frame = co_await receive(ws);
            if (!frame) break;
            for (uint32_t id : {1u, 2u}) {
                auto r = reply_of<api::SpectrumRow>(frame, id);
                if (r && r->freq_hz() == both) ids |= 1u << id;
            }
        }
        CHECK(ids == 0b110);

        co_await send(ws, cancel(1));
        CHECK(is_error(co_await receive(ws), 1, Error::CANCELLED));
        CHECK(!feeds.spectrum.all_receivers_dropped());
        ws = co_await connect(18708);  // closes the first connection, and its call
        CHECK(co_await read_within(feeds.spectrum, false));
    });
}

// The capture call `id`'s last report, after any progress reports before it, and then its end.
coro::Coro<std::optional<api::RecordCaptureProgress>> last_report(coro::WsStream& ws, uint32_t id) {
    for (int i = 0; i < 20; i++) {
        auto report = reply_of<api::RecordCaptureProgress>(co_await receive(ws), id);
        if (!report) co_return std::nullopt;
        if (!report->has_capture()) continue;
        if (!is_end(co_await receive(ws), id)) co_return std::nullopt;
        co_return report;
    }
    co_return std::nullopt;
}

// Whether the capture call `id` reports `samples` written within its next few progress reports, sent twice a
// second.
coro::Coro<bool> written(coro::WsStream& ws, uint32_t id, uint64_t samples) {
    for (int i = 0; i < 6; i++) {
        auto report = reply_of<api::RecordCaptureProgress>(co_await receive(ws), id);
        if (!report || report->has_capture()) co_return false;
        if (report->samples() == samples) co_return true;
    }
    co_return false;
}

// A capture directory of the test's own, empty.
std::filesystem::path capture_dir(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    return dir;
}

// The server checks a capture's request, and the call then records the point's blocks from those sent after it
// starts. Nothing reads a point before a capture of it, or after.
void test_capture() {
    const std::filesystem::path dir = capture_dir("api_server_test_capture");
    run(
        18709,
        [dir](coro::WsStream& ws, Feeds& feeds) -> coro::Coro<void> {
            co_await send(ws, record_capture(1, api::CAPTURE_POINT_UNSPECIFIED, 1, "a"));
            CHECK(is_error(co_await receive(ws), 1, Error::INVALID_ARGUMENT));
            co_await send(ws, record_capture(2, api::INPUT, 0, "a"));
            CHECK(is_error(co_await receive(ws), 2, Error::INVALID_ARGUMENT));
            co_await send(ws, record_capture(3, api::INPUT, CapturePoint::kMaxDurationS + 1, "a"));
            CHECK(is_error(co_await receive(ws), 3, Error::INVALID_ARGUMENT));
            uint32_t id = 4;
            for (const char* name : {"", ".a", "x/a"}) {
                co_await send(ws, record_capture(id, api::INPUT, 1, name));
                CHECK(is_error(co_await receive(ws), id, Error::INVALID_ARGUMENT));
                id++;
            }
            CHECK(feeds.input.all_receivers_dropped() && feeds.envelope.all_receivers_dropped());

            // 10 samples of the input, in three blocks, the last running past the end, with the receiver's fix.
            (void)feeds.receiver_position.send(position(1000, api::FIX_3D, 50, 10));
            co_await send(ws, record_capture(10, api::INPUT, 10 / kInputRateHz, "a"));
            CHECK(co_await read_within(feeds.input, true));
            for (uint64_t start : {0, 4, 8}) (void)feeds.input.send(block(4, start));
            auto last = co_await last_report(ws, 10);
            CHECK(last && last->samples() == 10 && last->missing() == 0 && last->start_index() == 0);
            CHECK(last && last->capture().name() == "a" && last->capture().data_bytes() == 80);
            CHECK(last && last->capture().meta().datatype() == "cf32_le");
            {
                std::ifstream meta(dir / "a.sigmf-meta");
                const std::string text{std::istreambuf_iterator<char>(meta), {}};
                CHECK(text.find("core:geolocation") != std::string::npos);
            }
            CHECK(last && last->capture().meta().rate_hz() == kInputRateHz);
            CHECK(last && last->capture().meta().freq_hz() == kFreqHz);
            CHECK(std::filesystem::file_size(dir / "a.sigmf-data") == 80);
            CHECK(std::filesystem::exists(dir / "a.sigmf-meta"));
            CHECK(co_await read_within(feeds.input, false));

            // A capture never overwrites one.
            co_await send(ws, record_capture(11, api::INPUT, 1, "a"));
            CHECK(is_error(co_await receive(ws), 11, Error::INVALID_ARGUMENT));

            // The envelope, as rf32_le, with no center frequency.
            co_await send(ws, record_capture(12, api::ENVELOPE, 10 / kEnvelopeRateHz, "b"));
            CHECK(co_await read_within(feeds.envelope, true));
            (void)feeds.envelope.send(envelope_run(6, 100));
            (void)feeds.envelope.send(envelope_run(6, 106));
            last = co_await last_report(ws, 12);
            CHECK(last && last->samples() == 10 && last->missing() == 0 && last->start_index() == 100);
            CHECK(last && last->capture().data_bytes() == 40);
            CHECK(last && last->capture().meta().datatype() == "rf32_le");
            CHECK(last && last->capture().meta().rate_hz() == kEnvelopeRateHz);
            CHECK(last && !last->capture().meta().has_freq_hz());
            CHECK(co_await read_within(feeds.envelope, false));

            // Samples that don't arrive are missing, and the capture covers its stream time all the same.
            co_await send(ws, record_capture(13, api::INPUT, 10 / kInputRateHz, "c"));
            CHECK(co_await read_within(feeds.input, true));
            (void)feeds.input.send(block(4, 0));
            (void)feeds.input.send(block(4, 6));
            last = co_await last_report(ws, 13);
            CHECK(last && last->samples() == 8 && last->missing() == 2);

            // Two of one point at once, each through its receiver. Blocks repeat until both have one; a capture
            // skips the samples it already has.
            co_await send(ws, record_capture(14, api::INPUT, 10 / kInputRateHz, "d"));
            co_await send(ws, record_capture(15, api::INPUT, 10 / kInputRateHz, "e"));
            bool both = false;
            for (int i = 0; i < 50 && !both; i++) {
                both = feeds.input.send(block(4, 0)).value_or(0) == 2;
                if (!both) co_await coro::sleep_for(20ms);
            }
            CHECK(both);
            for (uint64_t start : {4, 8}) (void)feeds.input.send(block(4, start));
            int done = 0;
            for (int i = 0; i < 20 && done < 4; i++) {
                auto frame = co_await receive(ws);
                if (!frame) break;
                for (uint32_t call : {14u, 15u}) {
                    if (is_end(frame, call)) done++;
                    auto r = reply_of<api::RecordCaptureProgress>(frame, call);
                    if (r && r->has_capture()) {
                        CHECK(r->samples() == 10 && r->missing() == 0);
                        done++;
                    }
                }
            }
            CHECK(done == 4);

            // A writer that falls behind the ring loses blocks, which are missing, rather than hold the input
            // back. After its first, the other 39 go at once, so it lags unless it keeps up.
            co_await send(ws, record_capture(16, api::INPUT, 160 / kInputRateHz, "f"));
            CHECK(co_await read_within(feeds.input, true));
            (void)feeds.input.send(block(4, 0));
            CHECK(co_await written(ws, 16, 4));
            for (uint64_t start = 4; start < 160; start += 4) (void)feeds.input.send(block(4, start));
            last = co_await last_report(ws, 16);
            CHECK(last && last->samples() + last->missing() == 160);

            // A cancel ends the capture, and its writer, keeping what it wrote.
            co_await send(ws, record_capture(17, api::INPUT, 1, "g"));
            CHECK(co_await read_within(feeds.input, true));
            (void)feeds.input.send(block(4, 0));
            CHECK(co_await written(ws, 17, 4));
            co_await send(ws, cancel(17));
            CHECK(is_error(co_await receive(ws), 17, Error::CANCELLED));
            CHECK(co_await read_within(feeds.input, false));
            CHECK(std::filesystem::file_size(dir / "g.sigmf-data") == 32);
            CHECK(std::filesystem::exists(dir / "g.sigmf-meta"));
        },
        {.capture_dir = dir.string(), .history = std::nullopt});
    std::filesystem::remove_all(dir);
}

// A capture running when the input ends finishes with what it has.
void test_capture_input_ends() {
    const std::filesystem::path dir = capture_dir("api_server_test_capture_ends");
    run(
        18714,
        [](coro::WsStream& ws, Feeds& feeds) -> coro::Coro<void> {
            co_await send(ws, record_capture(1, api::INPUT, 1, "a"));
            CHECK(co_await read_within(feeds.input, true));
            {
                coro::BroadcastSender<IqBlock> input = std::move(feeds.input);
                (void)input.send(block(4, 0));
            }
            auto last = co_await last_report(ws, 1);
            CHECK(last && last->samples() == 4 && last->capture().data_bytes() == 32);
        },
        {.capture_dir = dir.string(), .history = std::nullopt});
    std::filesystem::remove_all(dir);
}

// With captures off, the capture calls are refused, as is a call to a service that isn't served.
void test_captures_off() {
    run(
        18710,
        [](coro::WsStream& ws, Feeds&) -> coro::Coro<void> {
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
        [](coro::WsStream& ws, Feeds&) -> coro::Coro<void> {
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
// another without, and the receiver's position: a fix, then gpsd lost, then nothing recorded.
void test_history() {
    std::vector<api::Aircraft> rows = {
        recorded(0xabc, 1000, 1000, 50.0, 10.0),
        recorded(0xdef, 1005, 2000),
        recorded(0xabc, 1010, 1100, 50.1, 10.1),
    };
    rows[0].set_ground_speed_kt(300);
    run(
        18712,
        [](coro::WsStream& ws, Feeds&) -> coro::Coro<void> {
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

            // At 1003 the fix from 990 holds; at 1010, gpsd was lost; at 1200, nothing was recording.
            snap = co_await unary<api::Snapshot>(ws, history(6, "GetSnapshot", earlier));
            CHECK(snap && snap->has_receiver() && snap->receiver().state() == api::FIX_3D);
            CHECK(snap && snap->receiver().lat() == 50 && snap->receiver().lon() == 10);
            snap = co_await unary<api::Snapshot>(ws, history(7, "GetSnapshot", api::GetSnapshotRequest{}));
            CHECK(snap && snap->has_receiver() && snap->receiver().state() == api::GPSD_UNREACHABLE);
            api::GetSnapshotRequest later;
            later.set_t(1200);
            snap = co_await unary<api::Snapshot>(ws, history(8, "GetSnapshot", later));
            CHECK(snap && !snap->has_receiver());

            // From the row in effect at 1003.
            api::GetReceiverPositionsRequest range;
            range.set_from(1003);
            auto positions = co_await unary<api::ReceiverPositions>(ws, history(9, "GetReceiverPositions", range));
            CHECK(positions && positions->positions_size() == 2);
            CHECK(positions && positions->positions_size() == 2 && positions->positions(0).t() == 990 &&
                  positions->positions(1).state() == api::GPSD_UNREACHABLE);
        },
        {.live = false,
         .history = std::move(rows),
         .positions = {position(990, api::FIX_3D, 50, 10), position(1008, api::GPSD_UNREACHABLE)}});
}

// A client that disconnects with calls in progress doesn't disturb the next.
void test_disconnect() {
    run(18713, [](coro::WsStream& ws, Feeds& feeds) -> coro::Coro<void> {
        co_await send(ws, receiver(1, "WatchAircraft", api::WatchAircraftRequest{}));
        co_await send(ws, receiver(2, "WatchFrames", api::WatchFramesRequest{}));
        CHECK(co_await receive(ws));
        ws = co_await connect(18713);  // closes the first connection
        for (int i = 0; i < 3; i++) {
            (void)feeds.aircraft.send(LiveAircraft{{0xabc, aircraft(0xabc, 2000 + i)}});
            (void)feeds.frames.send(std::make_shared<const std::vector<api::Frame>>(1));
            co_await coro::sleep_for(kDefaultInterval);
        }
        co_await send(ws, hello({api::Receiver::kName}));
        CHECK(co_await receive(ws));
        auto info =
            co_await unary<api::ReceiverInfo>(ws, receiver(1, "GetReceiverInfo", api::GetReceiverInfoRequest{}));
        CHECK(info && info->source() == "file");
    });
}

// The UI API's history database opens a new file or one it wrote, upgrades one from the version before, and
// refuses one from any other version of adsb: one from before the version was recorded, with the table and
// version 0, or one with a newer version.
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
    const std::string v2 = make("api_server_test_v2.sqlite",
                                "CREATE TABLE aircraft_updates (t REAL NOT NULL, icao INTEGER NOT NULL, lat REAL, "
                                "lon REAL, altitude_ft INTEGER, ground_speed_kt REAL, vertical_rate_fpm INTEGER, "
                                "signal_db REAL, selected_altitude_ft INTEGER, state BLOB NOT NULL);"
                                "PRAGMA user_version = 2;");
    const std::string v4 = make("api_server_test_v4.sqlite", "PRAGMA user_version = 4;");

    coro::Runtime(2).block_on([](std::string fresh, std::string v1, std::string v2,
                                 std::string v4) -> coro::Coro<void> {
        CHECK(AircraftHistory::open(fresh));
        CHECK(AircraftHistory::open(fresh));  // again, now that it's been written
        CHECK(!AircraftHistory::open(v1));
        CHECK(AircraftHistory::open(v2));
        CHECK(!AircraftHistory::open(v4));
        co_return;
    }(fresh, v1, v2, v4));
    // The upgrade added the positions' table, and recorded the new version.
    sqlite3* db = nullptr;
    CHECK(sqlite3_open(v2.c_str(), &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db, "SELECT count(*) FROM receiver_positions;", nullptr, nullptr, nullptr) == SQLITE_OK);
    int version = 0;
    sqlite3_exec(
        db, "PRAGMA user_version;",
        [](void* v, int, char** values, char**) {
            *static_cast<int*>(v) = std::atoi(values[0]);
            return 0;
        },
        &version, nullptr);
    CHECK(version == 3);
    sqlite3_close(db);
    for (const std::string& path : {fresh, v1, v2, v4}) {
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
    test_receiver_position();
    test_spectrum();
    test_capture();
    test_capture_input_ends();
    test_captures_off();
    test_heatmap_area();
    test_history();
    test_disconnect();
    test_old_database();
    std::printf(g_failures ? "%d FAILURE(S)\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
