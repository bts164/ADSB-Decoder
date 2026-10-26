#include "output/aircraft_history.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <numbers>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <coro/stream.h>
#include <coro/task/spawn_blocking.h>
#include <sqlite3.h>
#include <xtensor/xbuilder.hpp>
#include <xtensor/xtensor.hpp>

#include "common/log.h"
#include "common/trace.h"

namespace {

// How far back from the requested time a snapshot looks: aircraft not updated within it are left out.
constexpr double kHistorySnapshotWindowS = 600;
// How far back from the requested time AircraftHistory::aircraft() looks.
constexpr double kHistoryAircraftWindowS = 1800;
// Bucket size of AircraftHistory::activity(), in s.
constexpr double kHistoryActivityBucketS = 60;
// Largest heatmap grid side AircraftHistory::heatmap() makes, in cells.
constexpr int kHistoryHeatmapMaxCells = 512;
// Most track points a snapshot carries per aircraft (the newest ones).
constexpr size_t kHistoryMaxTrackPoints = 1000;
// The schema's version, kept in the file's PRAGMA user_version. Files from before the protobuf API have 0.
constexpr int kSchemaVersion = 2;

// The statement is finalized on every exit path.
struct Statement {
    sqlite3_stmt* stmt = nullptr;
    ~Statement() { sqlite3_finalize(stmt); }
};

bool prepare(sqlite3* db, Statement& s, const char* sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &s.stmt, nullptr) == SQLITE_OK) return true;
    LOGF(ERROR, "history query failed: %s", sqlite3_errmsg(db));
    return false;
}

bool is_null(sqlite3_stmt* stmt, int col) { return sqlite3_column_type(stmt, col) == SQLITE_NULL; }

void bind_double(sqlite3_stmt* stmt, int col, bool has, double value) {
    if (has) sqlite3_bind_double(stmt, col, value);
    else sqlite3_bind_null(stmt, col);
}

void bind_int(sqlite3_stmt* stmt, int col, bool has, int64_t value) {
    if (has) sqlite3_bind_int64(stmt, col, value);
    else sqlite3_bind_null(stmt, col);
}

// The one integer the query `sql` returns, or nullopt with an ERROR logged.
std::optional<int64_t> query_int(sqlite3* db, const char* sql) {
    Statement s;
    if (!prepare(db, s, sql)) return std::nullopt;
    if (sqlite3_step(s.stmt) != SQLITE_ROW) {
        LOGF(ERROR, "history query failed: %s", sqlite3_errmsg(db));
        return std::nullopt;
    }
    return sqlite3_column_int64(s.stmt, 0);
}

// Web Mercator y of a latitude, in radians of arc, clamped to the square world maps show.
double mercator_y(double lat_deg) {
    const double lat = std::clamp(lat_deg, -85.051129, 85.051129) * std::numbers::pi / 180;
    return std::log(std::tan(std::numbers::pi / 4 + lat / 2));
}

// Clips the segment (x0, y0)-(x1, y1) to [0, w] x [0, h] (Liang-Barsky). False if it misses entirely.
bool clip_segment(double& x0, double& y0, double& x1, double& y1, double w, double h) {
    const double dx = x1 - x0, dy = y1 - y0;
    double t_in = 0, t_out = 1;
    // One edge: the segment is inside where p * t <= q.
    auto edge = [&](double p, double q) {
        if (p == 0) return q >= 0;
        const double r = q / p;
        if (p < 0) t_in = std::max(t_in, r);
        else t_out = std::min(t_out, r);
        return t_in <= t_out;
    };
    if (!edge(-dx, x0) || !edge(dx, w - x0) || !edge(-dy, y0) || !edge(dy, h - y0)) return false;
    x1 = x0 + t_out * dx;
    y1 = y0 + t_out * dy;
    x0 += t_in * dx;
    y0 += t_in * dy;
    return true;
}

// Counts flights per cell for AircraftHistory::heatmap(): each flight counts once in every cell it touches.
class FlightGrid {
public:
    FlightGrid(int width, int height)
        : m_width(width), m_height(height),
          m_counts(xt::zeros<uint32_t>({static_cast<size_t>(height), static_cast<size_t>(width)})),
          m_last_flight(m_counts.shape(), -1) {}

    // Starts the next flight; mark() and line() count it from here on.
    void next_flight() { m_flight++; }

    void mark(double x, double y) {
        if (!(x >= 0 && x < m_width && y >= 0 && y < m_height)) return;
        const auto row = static_cast<size_t>(y), col = static_cast<size_t>(x);
        if (m_last_flight(row, col) == m_flight) return;
        m_last_flight(row, col) = m_flight;
        m_counts(row, col)++;
    }

    // Marks the cells along the line, sampled at least once per cell.
    void line(double x0, double y0, double x1, double y1) {
        if (!clip_segment(x0, y0, x1, y1, m_width, m_height)) return;
        const int steps = static_cast<int>(std::ceil(std::max(std::abs(x1 - x0), std::abs(y1 - y0))));
        for (int s = 0; s <= steps; s++) {
            const double f = steps ? static_cast<double>(s) / steps : 0;
            mark(x0 + f * (x1 - x0), y0 + f * (y1 - y0));
        }
    }

    // Flights per cell, {height, width}.
    const xt::xtensor<uint32_t, 2>& counts() const { return m_counts; }

private:
    int m_width, m_height;
    xt::xtensor<uint32_t, 2> m_counts;
    xt::xtensor<int64_t, 2> m_last_flight;  // the last flight counted in each cell
    int64_t m_flight = 0;
};

// For std::visit: one lambda per alternative.
template<typename... Fs>
struct Overloaded : Fs... {
    using Fs::operator()...;
};

}  // namespace

// The database's one connection, used by the history task alone.
class AircraftHistory::Db {
public:
    // Opens the database, creating it if needed. Logs why on failure.
    static std::optional<Db> open(const std::string& db_path);

    Db(Db&& other) noexcept
        : m_db(std::exchange(other.m_db, nullptr)), m_insert(std::exchange(other.m_insert, nullptr)) {}
    Db& operator=(Db&&) = delete;
    ~Db() {
        sqlite3_finalize(m_insert);
        sqlite3_close(m_db);
    }

    void insert(const std::vector<api::Aircraft>& rows);
    std::optional<api::Snapshot> snapshot(const api::GetSnapshotRequest& req);
    std::optional<api::AircraftHistory> aircraft(const api::GetAircraftHistoryRequest& req);
    std::optional<api::Activity> activity(const api::GetActivityRequest& req);
    std::optional<api::Heatmap> heatmap(const api::GetHeatmapRequest& req);

private:
    explicit Db(sqlite3* db) : m_db(db) {}

    sqlite3* m_db;
    sqlite3_stmt* m_insert = nullptr;
};

std::optional<AircraftHistory::Db> AircraftHistory::Db::open(const std::string& db_path) {
    sqlite3* conn = nullptr;
    if (sqlite3_open(db_path.c_str(), &conn) != SQLITE_OK) {
        LOGF(ERROR, "failed to open history database %s: %s", db_path, sqlite3_errmsg(conn));
        sqlite3_close(conn);
        return std::nullopt;
    }
    Db self(conn);  // closes the connection on the failures below

    // Checked before anything below writes to the file. An old file has no version, but has the table.
    const std::optional<int64_t> version = query_int(conn, "PRAGMA user_version;");
    const std::optional<int64_t> tables =
        query_int(conn, "SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = 'aircraft_updates';");
    if (!version || !tables) return std::nullopt;
    if (*version != kSchemaVersion && (*version != 0 || *tables != 0)) {
        LOGF(ERROR, "history database %s is from %s adsb (schema version %d; this one reads %d): record to a new "
             "file", db_path, *version > kSchemaVersion ? "a newer" : "an older", *version, kSchemaVersion);
        return std::nullopt;
    }

    // Without WAL, another process reading the file (the sqlite3 shell, say) blocks the writes, and the demod
    // loop with them, so say so. The pragma doesn't fail when WAL is unavailable: it returns the mode it set.
    std::string journal_mode;
    sqlite3_exec(
        conn, "PRAGMA journal_mode=WAL;",
        [](void* mode, int, char** values, char**) {
            if (values[0]) *static_cast<std::string*>(mode) = values[0];
            return 0;
        },
        &journal_mode, nullptr);
    if (journal_mode != "wal")
        LOGF(WARNING, "history database %s: no WAL mode (journal mode \"%s\"); another process reading it will "
             "block recording", db_path, journal_mode);
    sqlite3_exec(conn, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    // t is last_seen_unix_s, and state the serialized api::Aircraft. The other columns are copied out of it
    // (null where it doesn't have them), so the queries read them without parsing every state.
    const std::string create_sql =
        "CREATE TABLE IF NOT EXISTS aircraft_updates ("
        "  t REAL NOT NULL,"
        "  icao INTEGER NOT NULL,"
        "  lat REAL,"
        "  lon REAL,"
        "  altitude_ft INTEGER,"
        "  ground_speed_kt REAL,"
        "  vertical_rate_fpm INTEGER,"
        "  signal_db REAL,"
        "  selected_altitude_ft INTEGER,"
        "  state BLOB NOT NULL"
        ");"
        // (t, icao) rather than just t, so AircraftHistory::activity() reads only the index.
        "CREATE INDEX IF NOT EXISTS idx_aircraft_updates_t_icao ON aircraft_updates(t, icao);"
        "CREATE INDEX IF NOT EXISTS idx_aircraft_updates_icao_t ON aircraft_updates(icao, t);"
        "PRAGMA user_version = " +
        std::to_string(kSchemaVersion) + ";";
    char* err = nullptr;
    if (sqlite3_exec(conn, create_sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        LOGF(ERROR, "history schema creation failed: %s", err);
        sqlite3_free(err);
        return std::nullopt;
    }

    if (sqlite3_prepare_v2(conn,
                           "INSERT INTO aircraft_updates (t, icao, lat, lon, altitude_ft, ground_speed_kt, "
                           "vertical_rate_fpm, signal_db, selected_altitude_ft, state) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
                           -1, &self.m_insert, nullptr) != SQLITE_OK) {
        LOGF(ERROR, "history insert statement failed: %s", sqlite3_errmsg(conn));
        return std::nullopt;
    }
    return self;
}

void AircraftHistory::Db::insert(const std::vector<api::Aircraft>& rows) {
    TRACE_ZONE_VALUE("history_insert", rows.size());
    // One transaction for the block, not one per row.
    sqlite3_exec(m_db, "BEGIN;", nullptr, nullptr, nullptr);
    for (const api::Aircraft& ac : rows) {
        const std::string state = ac.SerializeAsString();
        // A track point needs both, so a position is both or neither.
        const bool position = ac.has_lat() && ac.has_lon();

        sqlite3_reset(m_insert);
        sqlite3_bind_double(m_insert, 1, ac.last_seen_unix_s());
        sqlite3_bind_int64(m_insert, 2, ac.icao());
        bind_double(m_insert, 3, position, ac.lat());
        bind_double(m_insert, 4, position, ac.lon());
        bind_int(m_insert, 5, ac.has_altitude_ft(), ac.altitude_ft());
        bind_double(m_insert, 6, ac.has_ground_speed_kt(), ac.ground_speed_kt());
        bind_int(m_insert, 7, ac.has_vertical_rate_fpm(), ac.vertical_rate_fpm());
        bind_double(m_insert, 8, ac.has_signal_db(), ac.signal_db());
        bind_int(m_insert, 9, ac.has_selected_altitude_ft(), ac.selected_altitude_ft());
        sqlite3_bind_blob(m_insert, 10, state.data(), static_cast<int>(state.size()), SQLITE_TRANSIENT);
        if (sqlite3_step(m_insert) != SQLITE_DONE) LOGF(ERROR, "history insert failed: %s", sqlite3_errmsg(m_db));
    }
    if (sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK)
        LOGF(ERROR, "history commit failed: %s", sqlite3_errmsg(m_db));
}

std::optional<api::Snapshot> AircraftHistory::Db::snapshot(const api::GetSnapshotRequest& req) {
    TRACE_ZONE("history_snapshot");

    Statement range;
    if (!prepare(m_db, range, "SELECT min(t), max(t) FROM aircraft_updates;") || sqlite3_step(range.stmt) != SQLITE_ROW)
        return std::nullopt;
    api::Snapshot msg;
    if (!is_null(range.stmt, 0)) {
        msg.set_t_min(sqlite3_column_double(range.stmt, 0));
        msg.set_t_max(sqlite3_column_double(range.stmt, 1));
    }
    const double t = req.has_t() ? req.t() : msg.t_max();  // 0 for an empty database
    msg.set_t(t);
    const double t0 = t - kHistorySnapshotWindowS;

    // Each aircraft's latest state at t. SQLite takes a bare column alongside
    // max() from the row holding the max.
    Statement states;
    if (!prepare(m_db, states,
                 "SELECT icao, state, max(t) FROM aircraft_updates WHERE t > ?1 AND t <= ?2 GROUP BY icao;"))
        return std::nullopt;
    sqlite3_bind_double(states.stmt, 1, t0);
    sqlite3_bind_double(states.stmt, 2, t);
    std::unordered_map<uint32_t, int> index;  // each aircraft's place in msg.aircraft()
    while (sqlite3_step(states.stmt) == SQLITE_ROW) {
        const auto icao = static_cast<uint32_t>(sqlite3_column_int64(states.stmt, 0));
        index[icao] = msg.aircraft_size();
        if (!msg.add_aircraft()->mutable_state()->ParseFromArray(sqlite3_column_blob(states.stmt, 1),
                                                                 sqlite3_column_bytes(states.stmt, 1))) {
            LOGF(ERROR, "history snapshot failed: aircraft %06x has a state that doesn't parse", icao);
            return std::nullopt;
        }
    }

    Statement tracks;
    if (!prepare(m_db, tracks, "SELECT icao, lat, lon, altitude_ft FROM aircraft_updates "
                         "WHERE t > ?1 AND t <= ?2 AND lat IS NOT NULL ORDER BY t;"))
        return std::nullopt;
    sqlite3_bind_double(tracks.stmt, 1, t0);
    sqlite3_bind_double(tracks.stmt, 2, t);
    // Gathered first, so each keeps only its newest kHistoryMaxTrackPoints.
    std::vector<std::deque<api::TrackPoint>> track(msg.aircraft_size());
    while (sqlite3_step(tracks.stmt) == SQLITE_ROW) {
        // Always found: the states above cover the same window.
        auto it = index.find(static_cast<uint32_t>(sqlite3_column_int64(tracks.stmt, 0)));
        if (it == index.end()) continue;
        std::deque<api::TrackPoint>& points = track[it->second];
        api::TrackPoint& p = points.emplace_back();
        p.set_lat(sqlite3_column_double(tracks.stmt, 1));
        p.set_lon(sqlite3_column_double(tracks.stmt, 2));
        if (!is_null(tracks.stmt, 3)) p.set_altitude_ft(sqlite3_column_int(tracks.stmt, 3));
        if (points.size() > kHistoryMaxTrackPoints) points.pop_front();
    }
    for (int i = 0; i < msg.aircraft_size(); i++) {
        for (api::TrackPoint& p : track[i]) *msg.mutable_aircraft(i)->add_track() = std::move(p);
    }
    return msg;
}

std::optional<api::AircraftHistory> AircraftHistory::Db::aircraft(const api::GetAircraftHistoryRequest& req) {
    TRACE_ZONE("history_aircraft");

    Statement points;
    if (!prepare(m_db, points,
                 "SELECT t, altitude_ft, ground_speed_kt, vertical_rate_fpm, signal_db, selected_altitude_ft "
                 "FROM aircraft_updates WHERE icao = ?1 AND t > ?2 AND t <= ?3 ORDER BY t;"))
        return std::nullopt;
    // Without t, the window ends at this aircraft's latest update.
    double t = 0;
    if (req.has_t()) {
        t = req.t();
    } else {
        Statement latest;
        if (!prepare(m_db, latest, "SELECT max(t) FROM aircraft_updates WHERE icao = ?1;")) return std::nullopt;
        sqlite3_bind_int64(latest.stmt, 1, req.icao());
        if (sqlite3_step(latest.stmt) == SQLITE_ROW) t = sqlite3_column_double(latest.stmt, 0);
    }
    sqlite3_bind_int64(points.stmt, 1, req.icao());
    sqlite3_bind_double(points.stmt, 2, t - kHistoryAircraftWindowS);
    sqlite3_bind_double(points.stmt, 3, t);

    api::AircraftHistory msg;
    msg.set_t(t);
    while (sqlite3_step(points.stmt) == SQLITE_ROW) {
        api::AircraftHistory::Point& p = *msg.add_points();
        p.set_t(sqlite3_column_double(points.stmt, 0));
        if (!is_null(points.stmt, 1)) p.set_altitude_ft(sqlite3_column_int(points.stmt, 1));
        if (!is_null(points.stmt, 2)) p.set_ground_speed_kt(sqlite3_column_double(points.stmt, 2));
        if (!is_null(points.stmt, 3)) p.set_vertical_rate_fpm(sqlite3_column_int(points.stmt, 3));
        if (!is_null(points.stmt, 4)) p.set_signal_db(sqlite3_column_double(points.stmt, 4));
        if (!is_null(points.stmt, 5)) p.set_selected_altitude_ft(sqlite3_column_int(points.stmt, 5));
    }
    return msg;
}

std::optional<api::Activity> AircraftHistory::Db::activity(const api::GetActivityRequest& req) {
    TRACE_ZONE("history_activity");

    Statement counts;
    if (!prepare(m_db, counts, "SELECT CAST(t / ?2 AS INTEGER) AS b, count(DISTINCT icao) FROM aircraft_updates "
                         "WHERE t >= ?1 GROUP BY b ORDER BY b;"))
        return std::nullopt;
    const double from = req.has_from() ? std::floor(req.from() / kHistoryActivityBucketS) * kHistoryActivityBucketS
                                       : std::numeric_limits<double>::lowest();
    sqlite3_bind_double(counts.stmt, 1, from);
    sqlite3_bind_double(counts.stmt, 2, kHistoryActivityBucketS);

    api::Activity msg;
    msg.set_bucket_s(kHistoryActivityBucketS);
    std::vector<uint32_t> dense;
    int64_t first = 0;
    while (sqlite3_step(counts.stmt) == SQLITE_ROW) {
        const int64_t b = sqlite3_column_int64(counts.stmt, 0);
        if (dense.empty()) first = b;
        dense.resize(b - first + 1, 0);  // 0 for the empty buckets between rows.
        dense.back() = static_cast<uint32_t>(sqlite3_column_int64(counts.stmt, 1));
    }
    if (!dense.empty()) msg.set_t0(static_cast<double>(first) * kHistoryActivityBucketS);
    msg.mutable_counts()->Add(dense.begin(), dense.end());
    return msg;
}

std::optional<api::Heatmap> AircraftHistory::Db::heatmap(const api::GetHeatmapRequest& req) {
    if (!(req.east() > req.west()) || !(req.north() > req.south())) return std::nullopt;
    TRACE_ZONE("history_heatmap");
    const int width = static_cast<int>(std::clamp<uint32_t>(req.width(), 1, kHistoryHeatmapMaxCells));
    const int height = static_cast<int>(std::clamp<uint32_t>(req.height(), 1, kHistoryHeatmapMaxCells));

    // Positions less than kJoinS apart are joined; a gap of kNewFlightS or more starts a new flight.
    constexpr double kJoinS = 60;
    constexpr double kNewFlightS = 1800;

    Statement points;
    if (!prepare(m_db, points, "SELECT icao, t, lat, lon FROM aircraft_updates "
                         "WHERE lat IS NOT NULL AND t >= ?1 AND t <= ?2 ORDER BY icao, t;"))
        return std::nullopt;
    sqlite3_bind_double(points.stmt, 1, req.has_t0() ? req.t0() : std::numeric_limits<double>::lowest());
    sqlite3_bind_double(points.stmt, 2, req.has_t1() ? req.t1() : std::numeric_limits<double>::max());

    const double x_scale = width / (req.east() - req.west());
    const double y_north = mercator_y(req.north());
    const double y_scale = height / (y_north - mercator_y(req.south()));
    FlightGrid grid(width, height);
    std::optional<int64_t> prev_icao;
    double prev_t = 0, prev_x = 0, prev_y = 0;
    while (sqlite3_step(points.stmt) == SQLITE_ROW) {
        const int64_t icao = sqlite3_column_int64(points.stmt, 0);
        const double t = sqlite3_column_double(points.stmt, 1);
        const double x = (sqlite3_column_double(points.stmt, 3) - req.west()) * x_scale;
        const double y = (y_north - mercator_y(sqlite3_column_double(points.stmt, 2))) * y_scale;
        const bool same_aircraft = prev_icao == icao;
        if (!same_aircraft || t - prev_t >= kNewFlightS) grid.next_flight();
        if (same_aircraft && t - prev_t < kJoinS) grid.line(prev_x, prev_y, x, y);
        else grid.mark(x, y);
        prev_icao = icao;
        prev_t = t;
        prev_x = x;
        prev_y = y;
    }

    api::Heatmap msg;
    uint32_t max = 0;
    const auto& counts = grid.counts();
    // Cells are numbered row by row, which is the order the tensor stores them in.
    for (size_t i = 0; i < counts.size(); i++) {
        const uint32_t count = counts.flat(i);
        if (!count) continue;
        msg.add_cell_index(static_cast<uint32_t>(i));
        msg.add_cell_count(count);
        max = std::max(max, count);
    }
    msg.set_max(max);
    return msg;
}

std::optional<AircraftHistory> AircraftHistory::open(const std::string& db_path) {
    std::optional<Db> db = Db::open(db_path);
    if (!db) return std::nullopt;
    auto [tx, rx] = coro::mpsc_channel<Request>(kQueueRequests);
    // Detached: the runtime still cancels it at shutdown, and waits for it to finish.
    coro::spawn_blocking([rx = std::move(rx), db = std::move(*db)]() mutable { run(rx, db); }).detach();
    return AircraftHistory(std::move(tx));
}

coro::Coro<void> AircraftHistory::insert(std::vector<api::Aircraft> rows) {
    // Fails only once the history task has stopped, at runtime shutdown.
    (void)co_await m_tx.send(Insert{std::move(rows)});
}

template <typename Query, typename Result, typename Req>
coro::Coro<std::optional<Result>> AircraftHistory::ask(Req req) {
    auto [reply, result] = coro::oneshot_channel<std::optional<Result>>();
    if (!co_await m_tx.send(Query{std::move(req), std::move(reply)})) co_return std::nullopt;
    // An error only if the task dropped the reply: it threw, and logged why, or it was cancelled.
    auto answer = co_await result.recv();
    co_return answer ? std::move(*answer) : std::nullopt;
}

coro::Coro<std::optional<api::Snapshot>> AircraftHistory::snapshot(api::GetSnapshotRequest req) {
    return ask<Snapshot, api::Snapshot>(std::move(req));
}

coro::Coro<std::optional<api::AircraftHistory>> AircraftHistory::aircraft(api::GetAircraftHistoryRequest req) {
    return ask<Aircraft, api::AircraftHistory>(std::move(req));
}

coro::Coro<std::optional<api::Activity>> AircraftHistory::activity(api::GetActivityRequest req) {
    return ask<Activity, api::Activity>(std::move(req));
}

coro::Coro<std::optional<api::Heatmap>> AircraftHistory::heatmap(api::GetHeatmapRequest req) {
    return ask<Heatmap, api::Heatmap>(std::move(req));
}

void AircraftHistory::run(coro::MpscReceiver<Request>& rx, Db& db) {
    auto handle = [&db](Request& req) {
        // A query that throws drops its reply sender, so its caller gets nullopt. One whose caller has gone,
        // a WebSocket client that disconnected, is skipped.
        try {
            std::visit(Overloaded{
                           [&](Insert& r) { db.insert(r.rows); },
                           [&](Snapshot& r) {
                               if (!r.reply.is_closed()) (void)r.reply.send(db.snapshot(r.req));
                           },
                           [&](Aircraft& r) {
                               if (!r.reply.is_closed()) (void)r.reply.send(db.aircraft(r.req));
                           },
                           [&](Activity& r) {
                               if (!r.reply.is_closed()) (void)r.reply.send(db.activity(r.req));
                           },
                           [&](Heatmap& r) {
                               if (!r.reply.is_closed()) (void)r.reply.send(db.heatmap(r.req));
                           },
                       },
                       req);
        } catch (const std::exception& e) {
            LOGF(ERROR, "aircraft history: %s", e.what());
        }
    };
    try {
        while (auto req = coro::blocking_next(rx)) handle(*req);
    } catch (const coro::TaskCancelled&) {
        // The runtime is shutting down. try_recv() isn't a cancellation point, so the rows already sent can
        // still be written; the queries are dropped, and their callers get nullopt.
        while (auto req = rx.try_recv())
            if (auto* insert = std::get_if<Insert>(&*req)) db.insert(insert->rows);
        throw;
    }
}
