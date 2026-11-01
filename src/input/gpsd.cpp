#include "input/gpsd.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <coro/io/tcp_stream.h>
#include <coro/sync/sleep.h>
#include <coro/sync/timeout.h>
#include <google/protobuf/util/message_differencer.h>
#include <nlohmann/json.hpp>

#include "common/log.h"

namespace {

using std::chrono::steady_clock;

double unix_now_s() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// gpsd's time, such as "2026-10-31T12:34:56.000Z", in Unix s; nullopt if it doesn't parse.
std::optional<double> parse_gpsd_time(const std::string& s) {
    std::tm tm{};
    double sec = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%lf", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min,
                    &sec) != 6)
        return std::nullopt;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return static_cast<double>(timegm(&tm)) + sec;
}

// The reports' fields this reads, each optional in gpsd's protocol.
std::optional<double> number(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return std::nullopt;
    return it->get<double>();
}

// gpsd's reports, folded into the receiver's position, and the log lines its changes make.
class GpsdTracker {
public:
    // How long without a fix, from the start or from losing one, before a warning.
    static constexpr auto kNoFixWarning = std::chrono::minutes(2);
    // A move of more than this is logged.
    static constexpr double kMoveLoggedM = 50;
    // A system clock further than this from GPS time is logged.
    static constexpr double kClockOffS = 1;

    GpsdTracker(GpsdAddress address, coro::WatchSender<api::ReceiverPosition> position)
        : m_address(std::move(address)), m_position(std::move(position)) {
        m_now = gpsd_connecting(m_address);
        publish();
    }

    std::string where() const { return m_address.host + ":" + std::to_string(m_address.port); }

    void connected() {
        LOGF(INFO, "gps: connected to gpsd at %s", where());
        m_reachable = true;
        m_has_device.reset();
        m_now.set_state(api::NO_FIX);
        m_now.set_summary("no fix: waiting for gpsd's first report");
        publish();
    }

    // gpsd refused the connection, dropped it, or went silent.
    void unreachable(const std::string& why) {
        if (m_reachable) LOGF(INFO, "gps: lost gpsd at %s: %s; reconnecting every few seconds", where(), why);
        else VLOGF(1, "gps: gpsd at %s: %s", where(), why);
        if (has_fix()) lost_fix();
        m_reachable = false;
        m_now = {};
        m_now.set_state(api::GPSD_UNREACHABLE);
        m_now.set_summary("gpsd unreachable at " + where() + ": " + why);
        publish();
    }

    void report(const nlohmann::json& j) {
        const std::string kind = j.value("class", "");
        if (kind == "DEVICES") {
            auto devices = j.find("devices");
            m_has_device = devices != j.end() && devices->is_array() && !devices->empty();
            if (!*m_has_device && !has_fix()) {
                m_now.set_state(api::NO_FIX);
                m_now.set_summary("no fix: gpsd has no GPS device");
                publish();
            }
        } else if (kind == "SKY") {
            auto sats = j.find("satellites");
            if (sats == j.end() || !sats->is_array()) return;
            uint32_t used = 0;
            for (const nlohmann::json& s : *sats)
                if (s.value("used", false)) used++;
            m_now.set_satellites_used(used);
            m_now.set_satellites_seen(static_cast<uint32_t>(sats->size()));
            m_now.set_summary(summary());
            publish();
        } else if (kind == "TPV") {
            tpv(j);
        }
    }

    // Called at least every few seconds, to warn of a fix that hasn't come.
    void check_no_fix() {
        if (has_fix() || m_warned_no_fix || steady_clock::now() - m_no_fix_since < kNoFixWarning) return;
        m_warned_no_fix = true;
        const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(kNoFixWarning).count();
        LOGF(WARNING, "gps: no fix for %d minutes (%s): the receiver's position is unknown. Check the antenna, its "
             "view of the sky, and `systemctl status gps-ant-bias` on the ZynqMP", minutes, m_now.summary());
    }

private:
    bool has_fix() const { return m_now.state() == api::FIX_2D || m_now.state() == api::FIX_3D; }

    // Starts the time to the no-fix warning again.
    void lost_fix() {
        m_no_fix_since = steady_clock::now();
        m_warned_no_fix = false;
    }

    void tpv(const nlohmann::json& j) {
        const int mode = j.value("mode", 0);
        const std::optional<double> lat = number(j, "lat"), lon = number(j, "lon");
        const bool was_fix = has_fix();
        const api::GpsState was = m_now.state();
        const std::optional<uint32_t> used =
            m_now.has_satellites_used() ? std::optional(m_now.satellites_used()) : std::nullopt;
        const std::optional<uint32_t> seen =
            m_now.has_satellites_seen() ? std::optional(m_now.satellites_seen()) : std::nullopt;

        m_now = {};
        if (used) m_now.set_satellites_used(*used);
        if (seen) m_now.set_satellites_seen(*seen);
        if (mode >= 2 && lat && lon) {
            m_now.set_state(mode == 3 ? api::FIX_3D : api::FIX_2D);
            m_now.set_lat(*lat);
            m_now.set_lon(*lon);
            // altHAE is above the ellipsoid, as SigMF's geolocation wants; gpsd before 3.20 has only alt.
            std::optional<double> alt = number(j, "altHAE");
            if (!alt) alt = number(j, "alt");
            if (mode == 3 && alt) m_now.set_alt_m(*alt);
            std::optional<double> eph = number(j, "eph");
            if (!eph) {
                const std::optional<double> epx = number(j, "epx"), epy = number(j, "epy");
                if (epx && epy) eph = std::hypot(*epx, *epy);
            }
            if (eph) m_now.set_error_m(*eph);
        } else {
            m_now.set_state(api::NO_FIX);
        }
        m_now.set_summary(summary());

        if (has_fix() && m_now.state() != was) {
            LOGF(INFO, "gps: %s at %.6f, %.6f%s (%s)", was_fix ? "fix changed to" : m_had_fix ? "fix regained," :
                 "first fix,", m_now.lat(), m_now.lon(),
                 m_now.has_alt_m() ? absl::StrFormat(", %.0f m", m_now.alt_m()) : std::string(), m_now.summary());
            m_had_fix = true;
            m_logged = m_now;
        } else if (!has_fix() && was_fix) {
            LOGF(INFO, "gps: fix lost (%s)", m_now.summary());
            lost_fix();
        } else if (has_fix() && distance_m(m_logged, m_now) > kMoveLoggedM) {
            LOGF(INFO, "gps: moved %.0f m to %.6f, %.6f", distance_m(m_logged, m_now), m_now.lat(), m_now.lon());
            m_logged = m_now;
        }
        if (has_fix()) check_clock(j);
        publish();
    }

    // Warns when the system clock, which stamps the history, leaves GPS time, and when it comes back.
    void check_clock(const nlohmann::json& j) {
        const auto it = j.find("time");
        if (it == j.end() || !it->is_string()) return;
        const std::optional<double> gps = parse_gpsd_time(it->get<std::string>());
        if (!gps) return;
        const double off = unix_now_s() - *gps;
        const bool is_off = std::abs(off) > kClockOffS;
        if (is_off && !m_clock_off)
            LOGF(WARNING, "gps: the system clock is %+.1f s off GPS time, so the history's times are wrong: set it "
                 "with tools/gps_set_clock.py, or run chrony (doc/design/gps.md)", off);
        else if (!is_off && m_clock_off)
            LOGF(INFO, "gps: the system clock is within %g s of GPS time again", kClockOffS);
        m_clock_off = is_off;
    }

    std::string summary() const {
        std::string sats;
        if (m_now.has_satellites_used())
            sats = absl::StrFormat("%u of %u satellites", m_now.satellites_used(), m_now.satellites_seen());
        switch (m_now.state()) {
        case api::FIX_3D:
        case api::FIX_2D: {
            std::string s = m_now.state() == api::FIX_3D ? "3D fix" : "2D fix";
            if (!sats.empty()) s += ", " + sats;
            if (m_now.has_error_m()) s += absl::StrFormat(", ±%.0f m", m_now.error_m());
            return s;
        }
        case api::NO_FIX:
            if (m_has_device == false) return "no fix: gpsd has no GPS device";
            return sats.empty() ? "no fix: no satellites reported yet" : "no fix: " + sats + " used";
        default:
            return m_now.summary();
        }
    }

    // Sends the position if anything but its time changed.
    void publish() {
        m_now.set_t(unix_now_s());
        m_position.send_if_modified([&](api::ReceiverPosition& p) {
            api::ReceiverPosition now = m_now;
            now.set_t(p.t());
            if (google::protobuf::util::MessageDifferencer::Equals(p, now)) return false;
            p = m_now;
            return true;
        });
        check_no_fix();
    }

    GpsdAddress m_address;
    coro::WatchSender<api::ReceiverPosition> m_position;
    api::ReceiverPosition m_now;
    std::optional<bool> m_has_device;  // unset until gpsd's DEVICES report
    bool m_reachable = false;
    bool m_had_fix = false;
    api::ReceiverPosition m_logged;    // the position last logged, for the moves
    steady_clock::time_point m_no_fix_since = steady_clock::now();
    bool m_warned_no_fix = false;
    bool m_clock_off = false;
};

// Reads gpsd's reports from `stream` into `tracker` until the connection fails or goes silent.
coro::Coro<std::string> read_reports(coro::TcpStream& stream, GpsdTracker& tracker) {
    // gpsd sends a TPV a second with a device; without one it's quiet, so a silence is probed with ?DEVICES,
    // which it always answers. No answer to that, and the connection is taken as lost.
    constexpr auto kSilence = std::chrono::seconds(5);
    std::string pending;
    bool probed = false;
    for (;;) {
        auto woke = co_await coro::timeout(kSilence, stream.read(std::string(4096, '\0')));
        tracker.check_no_fix();
        if (woke.index() == 1) {
            if (probed) co_return "no report for 10 s";
            probed = true;
            (void)co_await stream.write(std::string("?DEVICES;\n"));
            continue;
        }
        auto [n, buf] = std::move(std::get<0>(woke).value);
        if (n == 0) co_return "it closed the connection";
        probed = false;
        pending.append(buf, 0, n);
        size_t start = 0;
        for (size_t end; (end = pending.find('\n', start)) != std::string::npos; start = end + 1) {
            const nlohmann::json j =
                nlohmann::json::parse(std::string_view(pending).substr(start, end - start), nullptr, false);
            if (j.is_discarded() || !j.is_object()) {
                VLOGF(1, "gps: a report from gpsd that isn't JSON: %s", pending.substr(start, end - start));
                continue;
            }
            VLOGF(2, "gps: %s", j.dump());
            tracker.report(j);
        }
        pending.erase(0, start);
    }
}

}  // namespace

api::ReceiverPosition receiver_without_gps(std::string summary) {
    api::ReceiverPosition p;
    p.set_t(unix_now_s());
    p.set_state(api::NO_GPS);
    p.set_summary(std::move(summary));
    return p;
}

api::ReceiverPosition gpsd_connecting(const GpsdAddress& address) {
    api::ReceiverPosition p;
    p.set_t(unix_now_s());
    p.set_state(api::GPSD_UNREACHABLE);
    p.set_summary("gpsd at " + address.host + ":" + std::to_string(address.port) + ": connecting");
    return p;
}

coro::Coro<void> watch_gpsd(GpsdAddress address, coro::WatchSender<api::ReceiverPosition> position) {
    constexpr auto kConnectTimeout = std::chrono::seconds(5);
    constexpr auto kRetry = std::chrono::seconds(5);
    LOGF(INFO, "gps: the receiver's position from gpsd at %s:%u", address.host, address.port);
    GpsdTracker tracker(address, std::move(position));
    for (;;) {
        std::string why;
        try {
            auto connected = co_await coro::timeout(kConnectTimeout,
                                                    coro::TcpStream::connect(address.host, address.port));
            if (connected.index() == 1) {
                why = "no answer in 5 s";
            } else {
                coro::TcpStream stream = std::move(std::get<0>(connected).value);
                (void)co_await stream.write(std::string("?WATCH={\"enable\":true,\"json\":true};\n"));
                tracker.connected();
                why = co_await read_reports(stream, tracker);
            }
        } catch (const std::exception& e) {
            why = e.what();
        }
        tracker.unreachable(why);
        co_await coro::sleep_for(kRetry);
        tracker.check_no_fix();
    }
}
