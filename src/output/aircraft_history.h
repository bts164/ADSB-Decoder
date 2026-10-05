#pragma once

#include <optional>
#include <string>

#include "decode/aircraft.h"

/**
 * @file
 * Aircraft history in a SQLite file: every published aircraft update, written live, and read back as the
 * state of the sky at any past time (aircraft_history_snapshot()) for the UI's time scrubber.
 *
 * Every function is a safe no-op until aircraft_history_init() succeeds, so callers needn't check whether
 * history is enabled.
 */

/** How far back from the requested time a snapshot looks: aircraft not updated within it are left out. */
constexpr double kHistorySnapshotWindowS = 600;

/** How far back from the requested time aircraft_history_aircraft() looks. */
constexpr double kHistoryAircraftWindowS = 1800;

/** Bucket size of aircraft_history_activity(), in s. */
constexpr double kHistoryActivityBucketS = 60;

/** Largest heatmap grid side aircraft_history_heatmap() makes, in cells. */
constexpr int kHistoryHeatmapMaxCells = 512;

/** Most track points a snapshot carries per aircraft (the newest ones). */
constexpr size_t kHistoryMaxTrackPoints = 1000;

/**
 * Opens (creating if needed) the database at `db_path` in WAL mode, plus a second, read-only connection for
 * snapshots, so reads from the WebSocket side never block the writer. Logs to stderr and stays disabled if
 * it can't be opened.
 * @return true if history can be queried (the read-only connection opened)
 */
bool aircraft_history_init(const std::string& db_path);

/**
 * Appends one aircraft update, keyed by `ac.last_seen_unix_s`.
 * @param ac the updated aircraft
 * @param json `ac` as published (`{"type":"aircraft",...}`), stored so a snapshot returns exactly that
 */
void aircraft_history_record(const AircraftState& ac, const std::string& json);

/**
 * The sky at `t_unix_s` (default: the latest update) as one
 * `{"type":"snapshot","t":...,"t_min":...,"t_max":...,"aircraft":[{"state":{...},"track":[[lat,lon,alt_ft],...]}]}`
 * message: each aircraft updated in the kHistorySnapshotWindowS before `t`, with its latest state at `t` and
 * its positions in the window, oldest first (alt_ft null where unknown). `t_min`/`t_max` span the whole database (null when it's empty).
 * Thread-safe; blocks on SQLite, so call it off the async runtime (coro::spawn_blocking).
 * @return the message, or nullopt if history is disabled or the query failed
 */
std::optional<std::string> aircraft_history_snapshot(std::optional<double> t_unix_s);

/**
 * One aircraft's recent history, for the UI's detail charts, as one
 * `{"type":"aircraft_history","icao":...,"t":...,"points":[[t,alt_ft,gs_kt,vr_fpm,signal_db,selected_alt_ft],...]}` message: its
 * updates in the kHistoryAircraftWindowS before `t_unix_s` (default: its latest update), oldest first, with
 * null for fields not yet known. Thread-safe; blocks on SQLite, so call it off the async runtime.
 * @param icao the aircraft's address, six lowercase hex digits
 * @return the message, or nullopt if history is disabled or the query failed
 */
std::optional<std::string> aircraft_history_aircraft(const std::string& icao, std::optional<double> t_unix_s);

/**
 * How busy the sky was, for the UI's scrubber, as one
 * `{"type":"activity","bucket_s":60,"t0":...,"counts":[...]}` message: the number of distinct aircraft
 * updated in each kHistoryActivityBucketS bucket (aligned to the Unix epoch), consecutive from the one
 * starting at `t0`, 0 where none were. Covers the buckets from the one holding `from_unix_s` (default:
 * the whole database) to the latest; `t0` is null and `counts` empty when there are none. The last bucket
 * may still be filling, so ask again from its start for an update. Thread-safe; blocks on SQLite (about
 * 15 ms per hour of history for the whole database), so call it off the async runtime.
 * @return the message, or nullopt if history is disabled or the query failed
 */
std::optional<std::string> aircraft_history_activity(std::optional<double> from_unix_s);

/** The area, grid and time range of a heatmap; see aircraft_history_heatmap(). */
struct HeatmapRequest {
    double west, south, east, north;  /**< degrees; east > west, north > south */
    int width, height;                /**< grid size in cells, clamped to 1..kHistoryHeatmapMaxCells */
    std::optional<double> t0, t1;     /**< Unix s; default the start and end of the database */
};

/**
 * Where aircraft flew, for the UI's map heatmap, as one
 * `{"type":"heatmap","west":...,"south":...,"east":...,"north":...,"width":...,"height":...,"t0":...,"t1":...,"max":...,"cells":[i,n,...]}`
 * message. The request's area is divided into a width x height grid, even in Web Mercator (so it overlays a
 * map as a stretched image), with row 0 at the north. `cells` lists the nonzero cells as pairs of row-major
 * index and count; `max` is the largest count.
 *
 * A cell's count is the number of flights that crossed it: each aircraft's consecutive positions less than
 * a minute apart are joined by a line, and every cell the line passes through counts that flight once. A
 * gap of 30 minutes or more starts a new flight. Counting flights rather than position updates keeps
 * aircraft near the receiver, heard more often, from dominating, and joining the positions fills in the
 * gaps between sparse ones far away.
 *
 * Thread-safe; blocks on SQLite and reads every position in the time range (about 0.2 s per 200k
 * positions), so call it off the async runtime.
 * @return the message, or nullopt if history is disabled, the area is empty or the query failed
 */
std::optional<std::string> aircraft_history_heatmap(const HeatmapRequest& req);
