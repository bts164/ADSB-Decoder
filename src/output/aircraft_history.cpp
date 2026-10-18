#include "output/aircraft_history.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <utility>
#include <vector>

#include <coro/stream.h>
#include <coro/task/spawn_blocking.h>
#include <nlohmann/json.hpp>
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

nlohmann::json column_or_null(sqlite3_stmt* stmt, int col) {
    return sqlite3_column_type(stmt, col) == SQLITE_NULL ? nlohmann::json(nullptr)
                                                         : nlohmann::json(sqlite3_column_double(stmt, col));
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

    void insert(const std::vector<HistoryRow>& rows);
    std::optional<std::string> snapshot(std::optional<double> t_unix_s);
    std::optional<std::string> aircraft(const std::string& icao, std::optional<double> t_unix_s);
    std::optional<std::string> activity(std::optional<double> from_unix_s);
    std::optional<std::string> heatmap(const HeatmapRequest& req);

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

    // t is last_seen_unix_s. lat/lon are copied out of the state JSON (null for
    // position-less updates) so tracks can be found by index; other fields are
    // read with json_extract().
    const char* create_sql =
        "CREATE TABLE IF NOT EXISTS aircraft_updates ("
        "  t REAL NOT NULL,"
        "  icao TEXT NOT NULL,"
        "  lat REAL,"
        "  lon REAL,"
        "  state TEXT NOT NULL"
        ");"
        // (t, icao) rather than just t, so AircraftHistory::activity() reads only the index. Replaces the
        // older idx_aircraft_updates_t.
        "DROP INDEX IF EXISTS idx_aircraft_updates_t;"
        "CREATE INDEX IF NOT EXISTS idx_aircraft_updates_t_icao ON aircraft_updates(t, icao);"
        "CREATE INDEX IF NOT EXISTS idx_aircraft_updates_icao_t ON aircraft_updates(icao, t);";
    char* err = nullptr;
    if (sqlite3_exec(conn, create_sql, nullptr, nullptr, &err) != SQLITE_OK) {
        LOGF(ERROR, "history schema creation failed: %s", err);
        sqlite3_free(err);
        return std::nullopt;
    }

    if (sqlite3_prepare_v2(conn, "INSERT INTO aircraft_updates (t, icao, lat, lon, state) VALUES (?, ?, ?, ?, ?);",
                           -1, &self.m_insert, nullptr) != SQLITE_OK) {
        LOGF(ERROR, "history insert statement failed: %s", sqlite3_errmsg(conn));
        return std::nullopt;
    }
    return self;
}

void AircraftHistory::Db::insert(const std::vector<HistoryRow>& rows) {
    TRACE_ZONE_VALUE("history_insert", rows.size());
    // One transaction for the block, not one per row.
    sqlite3_exec(m_db, "BEGIN;", nullptr, nullptr, nullptr);
    for (const HistoryRow& row : rows) {
        char icao_str[7];
        std::snprintf(icao_str, sizeof(icao_str), "%06x", row.icao);

        sqlite3_reset(m_insert);
        sqlite3_bind_double(m_insert, 1, row.t_unix_s);
        sqlite3_bind_text(m_insert, 2, icao_str, -1, SQLITE_TRANSIENT);
        if (row.lat && row.lon) {
            sqlite3_bind_double(m_insert, 3, *row.lat);
            sqlite3_bind_double(m_insert, 4, *row.lon);
        } else {
            sqlite3_bind_null(m_insert, 3);
            sqlite3_bind_null(m_insert, 4);
        }
        sqlite3_bind_text(m_insert, 5, row.state.data(), static_cast<int>(row.state.size()), SQLITE_TRANSIENT);
        if (sqlite3_step(m_insert) != SQLITE_DONE) LOGF(ERROR, "history insert failed: %s", sqlite3_errmsg(m_db));
    }
    if (sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK)
        LOGF(ERROR, "history commit failed: %s", sqlite3_errmsg(m_db));
}

std::optional<std::string> AircraftHistory::Db::snapshot(std::optional<double> t_unix_s) {
    TRACE_ZONE("history_snapshot");

    Statement range;
    if (!prepare(m_db, range, "SELECT min(t), max(t) FROM aircraft_updates;") || sqlite3_step(range.stmt) != SQLITE_ROW)
        return std::nullopt;
    nlohmann::json msg;
    msg["type"] = "snapshot";
    msg["t_min"] = column_or_null(range.stmt, 0);
    msg["t_max"] = column_or_null(range.stmt, 1);
    const double t = t_unix_s.value_or(msg["t_max"].is_null() ? 0.0 : msg["t_max"].get<double>());
    msg["t"] = t;
    msg["aircraft"] = nlohmann::json::array();
    const double t0 = t - kHistorySnapshotWindowS;

    // Each aircraft's latest state at t. SQLite takes a bare column alongside
    // max() from the row holding the max.
    Statement states;
    if (!prepare(m_db, states, "SELECT icao, state, max(t) FROM aircraft_updates WHERE t > ?1 AND t <= ?2 GROUP BY icao;"))
        return std::nullopt;
    sqlite3_bind_double(states.stmt, 1, t0);
    sqlite3_bind_double(states.stmt, 2, t);
    std::vector<std::string> order;
    std::unordered_map<std::string, std::pair<std::string, std::deque<nlohmann::json>>> aircraft;
    while (sqlite3_step(states.stmt) == SQLITE_ROW) {
        std::string icao = reinterpret_cast<const char*>(sqlite3_column_text(states.stmt, 0));
        order.push_back(icao);
        aircraft[icao].first = reinterpret_cast<const char*>(sqlite3_column_text(states.stmt, 1));
    }

    Statement tracks;
    if (!prepare(m_db, tracks, "SELECT icao, lat, lon, json_extract(state, '$.altitude_ft') FROM aircraft_updates "
                         "WHERE t > ?1 AND t <= ?2 AND lat IS NOT NULL ORDER BY t;"))
        return std::nullopt;
    sqlite3_bind_double(tracks.stmt, 1, t0);
    sqlite3_bind_double(tracks.stmt, 2, t);
    while (sqlite3_step(tracks.stmt) == SQLITE_ROW) {
        auto& track = aircraft[reinterpret_cast<const char*>(sqlite3_column_text(tracks.stmt, 0))].second;
        track.push_back({sqlite3_column_double(tracks.stmt, 1), sqlite3_column_double(tracks.stmt, 2),
                         column_or_null(tracks.stmt, 3)});
        if (track.size() > kHistoryMaxTrackPoints) track.pop_front();
    }

    for (const auto& icao : order) {
        const auto& [state, track] = aircraft[icao];
        nlohmann::json points = track;
        msg["aircraft"].push_back({{"state", nlohmann::json::parse(state)}, {"track", std::move(points)}});
    }
    return msg.dump();
}

std::optional<std::string> AircraftHistory::Db::aircraft(const std::string& icao, std::optional<double> t_unix_s) {
    TRACE_ZONE("history_aircraft");

    Statement points;
    if (!prepare(m_db, points, "SELECT t, json_extract(state, '$.altitude_ft'), json_extract(state, '$.ground_speed_kt'), "
                         "json_extract(state, '$.vertical_rate_fpm'), json_extract(state, '$.signal_db'), "
                         "json_extract(state, '$.selected_altitude_ft') "
                         "FROM aircraft_updates WHERE icao = ?1 AND t > ?2 AND t <= ?3 ORDER BY t;"))
        return std::nullopt;
    // Without t, the window ends at this aircraft's latest update.
    double t = 0;
    if (t_unix_s) {
        t = *t_unix_s;
    } else {
        Statement latest;
        if (!prepare(m_db, latest, "SELECT max(t) FROM aircraft_updates WHERE icao = ?1;")) return std::nullopt;
        sqlite3_bind_text(latest.stmt, 1, icao.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(latest.stmt) == SQLITE_ROW) t = sqlite3_column_double(latest.stmt, 0);
    }
    sqlite3_bind_text(points.stmt, 1, icao.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(points.stmt, 2, t - kHistoryAircraftWindowS);
    sqlite3_bind_double(points.stmt, 3, t);

    nlohmann::json msg;
    msg["type"] = "aircraft_history";
    msg["icao"] = icao;
    msg["t"] = t;
    msg["points"] = nlohmann::json::array();
    while (sqlite3_step(points.stmt) == SQLITE_ROW) {
        msg["points"].push_back({sqlite3_column_double(points.stmt, 0), column_or_null(points.stmt, 1),
                                 column_or_null(points.stmt, 2), column_or_null(points.stmt, 3),
                                 column_or_null(points.stmt, 4), column_or_null(points.stmt, 5)});
    }
    return msg.dump();
}

std::optional<std::string> AircraftHistory::Db::activity(std::optional<double> from_unix_s) {
    TRACE_ZONE("history_activity");

    Statement counts;
    if (!prepare(m_db, counts, "SELECT CAST(t / ?2 AS INTEGER) AS b, count(DISTINCT icao) FROM aircraft_updates "
                         "WHERE t >= ?1 GROUP BY b ORDER BY b;"))
        return std::nullopt;
    const double from = from_unix_s ? std::floor(*from_unix_s / kHistoryActivityBucketS) * kHistoryActivityBucketS
                                    : std::numeric_limits<double>::lowest();
    sqlite3_bind_double(counts.stmt, 1, from);
    sqlite3_bind_double(counts.stmt, 2, kHistoryActivityBucketS);

    nlohmann::json msg;
    msg["type"] = "activity";
    msg["bucket_s"] = kHistoryActivityBucketS;
    msg["t0"] = nullptr;
    std::vector<int64_t> dense;
    int64_t first = 0;
    while (sqlite3_step(counts.stmt) == SQLITE_ROW) {
        const int64_t b = sqlite3_column_int64(counts.stmt, 0);
        if (dense.empty()) first = b;
        dense.resize(b - first + 1, 0);  // 0 for the empty buckets between rows.
        dense.back() = sqlite3_column_int64(counts.stmt, 1);
    }
    if (!dense.empty()) msg["t0"] = static_cast<double>(first) * kHistoryActivityBucketS;
    msg["counts"] = dense;
    return msg.dump();
}

std::optional<std::string> AircraftHistory::Db::heatmap(const HeatmapRequest& req) {
    if (!(req.east > req.west) || !(req.north > req.south)) return std::nullopt;
    TRACE_ZONE("history_heatmap");
    const int width = std::clamp(req.width, 1, kHistoryHeatmapMaxCells);
    const int height = std::clamp(req.height, 1, kHistoryHeatmapMaxCells);

    // Positions less than kJoinS apart are joined; a gap of kNewFlightS or more starts a new flight.
    constexpr double kJoinS = 60;
    constexpr double kNewFlightS = 1800;

    Statement points;
    if (!prepare(m_db, points, "SELECT icao, t, lat, lon FROM aircraft_updates "
                         "WHERE lat IS NOT NULL AND t >= ?1 AND t <= ?2 ORDER BY icao, t;"))
        return std::nullopt;
    sqlite3_bind_double(points.stmt, 1, req.t0.value_or(std::numeric_limits<double>::lowest()));
    sqlite3_bind_double(points.stmt, 2, req.t1.value_or(std::numeric_limits<double>::max()));

    const double x_scale = width / (req.east - req.west);
    const double y_north = mercator_y(req.north);
    const double y_scale = height / (y_north - mercator_y(req.south));
    FlightGrid grid(width, height);
    std::string prev_icao;
    double prev_t = 0, prev_x = 0, prev_y = 0;
    while (sqlite3_step(points.stmt) == SQLITE_ROW) {
        const char* icao = reinterpret_cast<const char*>(sqlite3_column_text(points.stmt, 0));
        const double t = sqlite3_column_double(points.stmt, 1);
        const double x = (sqlite3_column_double(points.stmt, 3) - req.west) * x_scale;
        const double y = (y_north - mercator_y(sqlite3_column_double(points.stmt, 2))) * y_scale;
        const bool same_aircraft = prev_icao == icao;
        if (!same_aircraft || t - prev_t >= kNewFlightS) grid.next_flight();
        if (same_aircraft && t - prev_t < kJoinS) grid.line(prev_x, prev_y, x, y);
        else grid.mark(x, y);
        if (!same_aircraft) prev_icao = icao;
        prev_t = t;
        prev_x = x;
        prev_y = y;
    }

    nlohmann::json msg;
    msg["type"] = "heatmap";
    msg["west"] = req.west;
    msg["south"] = req.south;
    msg["east"] = req.east;
    msg["north"] = req.north;
    msg["width"] = width;
    msg["height"] = height;
    msg["t0"] = req.t0 ? nlohmann::json(*req.t0) : nlohmann::json(nullptr);
    msg["t1"] = req.t1 ? nlohmann::json(*req.t1) : nlohmann::json(nullptr);
    std::vector<uint32_t> cells;
    uint32_t max = 0;
    const auto& counts = grid.counts();
    // Cells are numbered row by row, which is the order the tensor stores them in.
    for (size_t i = 0; i < counts.size(); i++) {
        const uint32_t count = counts.flat(i);
        if (!count) continue;
        cells.push_back(static_cast<uint32_t>(i));
        cells.push_back(count);
        max = std::max(max, count);
    }
    msg["max"] = max;
    msg["cells"] = std::move(cells);
    return msg.dump();
}

std::optional<AircraftHistory> AircraftHistory::open(const std::string& db_path) {
    std::optional<Db> db = Db::open(db_path);
    if (!db) return std::nullopt;
    auto [tx, rx] = coro::mpsc_channel<Request>(kQueueRequests);
    // Detached: the runtime still cancels it at shutdown, and waits for it to finish.
    coro::spawn_blocking([rx = std::move(rx), db = std::move(*db)]() mutable { run(rx, db); }).detach();
    return AircraftHistory(std::move(tx));
}

coro::Coro<void> AircraftHistory::insert(std::vector<HistoryRow> rows) {
    // Fails only once the history task has stopped, at runtime shutdown.
    (void)co_await m_tx.send(Insert{std::move(rows)});
}

coro::Coro<std::optional<std::string>> AircraftHistory::snapshot(std::optional<double> t_unix_s) {
    auto [reply, result] = coro::oneshot_channel<std::optional<std::string>>();
    co_return co_await ask(Snapshot{t_unix_s, std::move(reply)}, std::move(result));
}

coro::Coro<std::optional<std::string>> AircraftHistory::aircraft(std::string icao, std::optional<double> t_unix_s) {
    auto [reply, result] = coro::oneshot_channel<std::optional<std::string>>();
    co_return co_await ask(Aircraft{std::move(icao), t_unix_s, std::move(reply)}, std::move(result));
}

coro::Coro<std::optional<std::string>> AircraftHistory::activity(std::optional<double> from_unix_s) {
    auto [reply, result] = coro::oneshot_channel<std::optional<std::string>>();
    co_return co_await ask(Activity{from_unix_s, std::move(reply)}, std::move(result));
}

coro::Coro<std::optional<std::string>> AircraftHistory::heatmap(HeatmapRequest req) {
    auto [reply, result] = coro::oneshot_channel<std::optional<std::string>>();
    co_return co_await ask(Heatmap{req, std::move(reply)}, std::move(result));
}

coro::Coro<std::optional<std::string>> AircraftHistory::ask(
    Request query, coro::OneshotReceiver<std::optional<std::string>> result) {
    if (!co_await m_tx.send(std::move(query))) co_return std::nullopt;
    // An error only if the task dropped the reply: it threw, and logged why, or it was cancelled.
    auto reply = co_await result.recv();
    co_return reply ? std::move(*reply) : std::nullopt;
}

void AircraftHistory::run(coro::MpscReceiver<Request>& rx, Db& db) {
    auto handle = [&db](Request& req) {
        // A query that throws drops its reply sender, so its caller gets nullopt.
        try {
            std::visit(Overloaded{
                           [&](Insert& r) { db.insert(r.rows); },
                           [&](Snapshot& r) { (void)r.reply.send(db.snapshot(r.t_unix_s)); },
                           [&](Aircraft& r) { (void)r.reply.send(db.aircraft(r.icao, r.t_unix_s)); },
                           [&](Activity& r) { (void)r.reply.send(db.activity(r.from_unix_s)); },
                           [&](Heatmap& r) { (void)r.reply.send(db.heatmap(r.req)); },
                       },
                       req);
        } catch (const std::exception& e) {
            LOGF(ERROR, "aircraft history: %s", e.what());
        }
    };
    try {
        while (auto req = coro::blocking_next(rx)) handle(*req);
    } catch (const coro::BlockingCancelled&) {
        // The runtime is shutting down. try_recv() isn't a cancellation point, so the rows already sent can
        // still be written; the queries are dropped, and their callers get nullopt.
        while (auto req = rx.try_recv())
            if (auto* insert = std::get_if<Insert>(&*req)) db.insert(insert->rows);
        throw;
    }
}
