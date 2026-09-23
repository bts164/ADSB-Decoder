#include "aircraft_history.h"

#include <cstdio>
#include <iostream>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

namespace {

// Static-duration, so record()/backfill_messages() are safe no-ops for the
// entire process lifetime until aircraft_history_init() runs -- same
// pattern as ws_publisher.cpp's g_tx.
sqlite3* g_db = nullptr;
sqlite3_stmt* g_insert_stmt = nullptr;

}  // namespace

void aircraft_history_init(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &g_db) != SQLITE_OK) {
        std::cerr << "[history] failed to open " << db_path << ": " << sqlite3_errmsg(g_db) << "\n";
        sqlite3_close(g_db);
        g_db = nullptr;
        return;
    }
    // WAL so the backfill query (run from ws_publisher's thread) never
    // blocks on -- or is blocked by -- the live insert writer thread.
    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(g_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    const char* create_sql =
        "CREATE TABLE IF NOT EXISTS positions ("
        "  icao TEXT NOT NULL,"
        "  t REAL NOT NULL,"
        "  lat REAL NOT NULL,"
        "  lon REAL NOT NULL,"
        "  altitude_ft INTEGER"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_positions_icao_t ON positions(icao, t);";
    char* err = nullptr;
    if (sqlite3_exec(g_db, create_sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "[history] schema creation failed: " << err << "\n";
        sqlite3_free(err);
        sqlite3_close(g_db);
        g_db = nullptr;
        return;
    }

    sqlite3_prepare_v2(g_db, "INSERT INTO positions (icao, t, lat, lon, altitude_ft) VALUES (?, ?, ?, ?, ?);", -1,
                        &g_insert_stmt, nullptr);
}

void aircraft_history_record(const AircraftState& ac) {
    if (!g_db || !g_insert_stmt || !ac.lat || !ac.lon) return;

    char icao_str[7];
    std::snprintf(icao_str, sizeof(icao_str), "%06x", ac.icao);

    sqlite3_reset(g_insert_stmt);
    sqlite3_bind_text(g_insert_stmt, 1, icao_str, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(g_insert_stmt, 2, ac.last_seen_s);
    sqlite3_bind_double(g_insert_stmt, 3, *ac.lat);
    sqlite3_bind_double(g_insert_stmt, 4, *ac.lon);
    if (ac.altitude_ft) {
        sqlite3_bind_int(g_insert_stmt, 5, *ac.altitude_ft);
    } else {
        sqlite3_bind_null(g_insert_stmt, 5);
    }
    if (sqlite3_step(g_insert_stmt) != SQLITE_DONE) {
        std::cerr << "[history] insert failed: " << sqlite3_errmsg(g_db) << "\n";
    }
}

std::vector<std::string> aircraft_history_backfill_messages() {
    std::vector<std::string> out;
    if (!g_db) return out;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, "SELECT icao, t, lat, lon, altitude_ft FROM positions ORDER BY icao, t;", -1, &stmt,
                            nullptr) != SQLITE_OK) {
        std::cerr << "[history] backfill query failed: " << sqlite3_errmsg(g_db) << "\n";
        return out;
    }

    // Grouped by icao while preserving first-seen order, so each aircraft's
    // track goes out as a single message rather than one message per point.
    std::vector<std::string> order;
    std::unordered_map<std::string, nlohmann::json> tracks;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string icao = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

        auto it = tracks.find(icao);
        if (it == tracks.end()) {
            order.push_back(icao);
            nlohmann::json msg;
            msg["type"] = "history";
            msg["icao"] = icao;
            msg["track"] = nlohmann::json::array();
            it = tracks.emplace(icao, std::move(msg)).first;
        }

        nlohmann::json point;
        point["t"] = sqlite3_column_double(stmt, 1);
        point["lat"] = sqlite3_column_double(stmt, 2);
        point["lon"] = sqlite3_column_double(stmt, 3);
        point["altitude_ft"] =
            sqlite3_column_type(stmt, 4) != SQLITE_NULL ? nlohmann::json(sqlite3_column_int(stmt, 4)) : nullptr;
        it->second["track"].push_back(std::move(point));
    }
    sqlite3_finalize(stmt);

    out.reserve(order.size());
    for (auto& icao : order) out.push_back(tracks[icao].dump());
    return out;
}
