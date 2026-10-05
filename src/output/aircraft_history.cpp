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

#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <xtensor/xbuilder.hpp>
#include <xtensor/xtensor.hpp>

#include "common/log.h"

namespace {

// Static-duration, so record()/snapshot() are safe no-ops for the entire
// process lifetime until aircraft_history_init() runs -- same pattern as
// ws_publisher.cpp's g_tx.
sqlite3* g_db = nullptr;
sqlite3_stmt* g_insert_stmt = nullptr;
// Read-only, opened in serialized (FULLMUTEX) mode since snapshots for
// several clients can run at once on spawn_blocking threads.
sqlite3* g_read_db = nullptr;

// The statement is finalized on every exit path.
struct Statement {
    sqlite3_stmt* stmt = nullptr;
    ~Statement() { sqlite3_finalize(stmt); }
};

bool prepare(Statement& s, const char* sql) {
    if (sqlite3_prepare_v2(g_read_db, sql, -1, &s.stmt, nullptr) == SQLITE_OK) return true;
    LOGF(ERROR, "history query failed: %s", sqlite3_errmsg(g_read_db));
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

// Counts flights per cell for aircraft_history_heatmap(): each flight counts once in every cell it touches.
class FlightGrid {
public:
    FlightGrid(int width, int height)
        : width_(width), height_(height),
          counts_(xt::zeros<uint32_t>({static_cast<size_t>(height), static_cast<size_t>(width)})),
          last_flight_(counts_.shape(), -1) {}

    // Starts the next flight; mark() and line() count it from here on.
    void next_flight() { flight_++; }

    void mark(double x, double y) {
        if (!(x >= 0 && x < width_ && y >= 0 && y < height_)) return;
        const auto row = static_cast<size_t>(y), col = static_cast<size_t>(x);
        if (last_flight_(row, col) == flight_) return;
        last_flight_(row, col) = flight_;
        counts_(row, col)++;
    }

    // Marks the cells along the line, sampled at least once per cell.
    void line(double x0, double y0, double x1, double y1) {
        if (!clip_segment(x0, y0, x1, y1, width_, height_)) return;
        const int steps = static_cast<int>(std::ceil(std::max(std::abs(x1 - x0), std::abs(y1 - y0))));
        for (int s = 0; s <= steps; s++) {
            const double f = steps ? static_cast<double>(s) / steps : 0;
            mark(x0 + f * (x1 - x0), y0 + f * (y1 - y0));
        }
    }

    // Flights per cell, {height, width}.
    const xt::xtensor<uint32_t, 2>& counts() const { return counts_; }

private:
    int width_, height_;
    xt::xtensor<uint32_t, 2> counts_;
    xt::xtensor<int64_t, 2> last_flight_;  // the last flight counted in each cell
    int64_t flight_ = 0;
};

}  // namespace

bool aircraft_history_init(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &g_db) != SQLITE_OK) {
        LOGF(ERROR, "failed to open history database %s: %s", db_path, sqlite3_errmsg(g_db));
        sqlite3_close(g_db);
        g_db = nullptr;
        return false;
    }
    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(g_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

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
        // (t, icao) rather than just t, so aircraft_history_activity() reads only the index. Replaces the
        // older idx_aircraft_updates_t.
        "DROP INDEX IF EXISTS idx_aircraft_updates_t;"
        "CREATE INDEX IF NOT EXISTS idx_aircraft_updates_t_icao ON aircraft_updates(t, icao);"
        "CREATE INDEX IF NOT EXISTS idx_aircraft_updates_icao_t ON aircraft_updates(icao, t);";
    char* err = nullptr;
    if (sqlite3_exec(g_db, create_sql, nullptr, nullptr, &err) != SQLITE_OK) {
        LOGF(ERROR, "history schema creation failed: %s", err);
        sqlite3_free(err);
        sqlite3_close(g_db);
        g_db = nullptr;
        return false;
    }

    sqlite3_prepare_v2(g_db, "INSERT INTO aircraft_updates (t, icao, lat, lon, state) VALUES (?, ?, ?, ?, ?);", -1,
                        &g_insert_stmt, nullptr);

    if (sqlite3_open_v2(db_path.c_str(), &g_read_db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) !=
        SQLITE_OK) {
        LOGF(ERROR, "failed to open history database %s for reading: %s", db_path, sqlite3_errmsg(g_read_db));
        sqlite3_close(g_read_db);
        g_read_db = nullptr;
        return false;
    }
    return true;
}

void aircraft_history_record(const AircraftState& ac, const std::string& json) {
    if (!g_db || !g_insert_stmt) return;

    char icao_str[7];
    std::snprintf(icao_str, sizeof(icao_str), "%06x", ac.icao);

    sqlite3_reset(g_insert_stmt);
    sqlite3_bind_double(g_insert_stmt, 1, ac.last_seen_unix_s);
    sqlite3_bind_text(g_insert_stmt, 2, icao_str, -1, SQLITE_TRANSIENT);
    if (ac.lat && ac.lon) {
        sqlite3_bind_double(g_insert_stmt, 3, *ac.lat);
        sqlite3_bind_double(g_insert_stmt, 4, *ac.lon);
    } else {
        sqlite3_bind_null(g_insert_stmt, 3);
        sqlite3_bind_null(g_insert_stmt, 4);
    }
    sqlite3_bind_text(g_insert_stmt, 5, json.data(), static_cast<int>(json.size()), SQLITE_TRANSIENT);
    if (sqlite3_step(g_insert_stmt) != SQLITE_DONE) {
        LOGF(ERROR, "history insert failed: %s", sqlite3_errmsg(g_db));
    }
}

std::optional<std::string> aircraft_history_snapshot(std::optional<double> t_unix_s) {
    if (!g_read_db) return std::nullopt;

    // One read transaction, so the range, states and tracks all see the same
    // database even as the writer appends.
    sqlite3_exec(g_read_db, "BEGIN;", nullptr, nullptr, nullptr);
    struct EndRead {
        ~EndRead() { sqlite3_exec(g_read_db, "COMMIT;", nullptr, nullptr, nullptr); }
    } end_read;

    Statement range;
    if (!prepare(range, "SELECT min(t), max(t) FROM aircraft_updates;") || sqlite3_step(range.stmt) != SQLITE_ROW)
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
    if (!prepare(states, "SELECT icao, state, max(t) FROM aircraft_updates WHERE t > ?1 AND t <= ?2 GROUP BY icao;"))
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
    if (!prepare(tracks, "SELECT icao, lat, lon, json_extract(state, '$.altitude_ft') FROM aircraft_updates "
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

std::optional<std::string> aircraft_history_aircraft(const std::string& icao, std::optional<double> t_unix_s) {
    if (!g_read_db) return std::nullopt;

    Statement points;
    if (!prepare(points, "SELECT t, json_extract(state, '$.altitude_ft'), json_extract(state, '$.ground_speed_kt'), "
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
        if (!prepare(latest, "SELECT max(t) FROM aircraft_updates WHERE icao = ?1;")) return std::nullopt;
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

std::optional<std::string> aircraft_history_activity(std::optional<double> from_unix_s) {
    if (!g_read_db) return std::nullopt;

    Statement counts;
    if (!prepare(counts, "SELECT CAST(t / ?2 AS INTEGER) AS b, count(DISTINCT icao) FROM aircraft_updates "
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

std::optional<std::string> aircraft_history_heatmap(const HeatmapRequest& req) {
    if (!g_read_db || !(req.east > req.west) || !(req.north > req.south)) return std::nullopt;
    const int width = std::clamp(req.width, 1, kHistoryHeatmapMaxCells);
    const int height = std::clamp(req.height, 1, kHistoryHeatmapMaxCells);

    // Positions less than kJoinS apart are joined; a gap of kNewFlightS or more starts a new flight.
    constexpr double kJoinS = 60;
    constexpr double kNewFlightS = 1800;

    Statement points;
    if (!prepare(points, "SELECT icao, t, lat, lon FROM aircraft_updates "
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
