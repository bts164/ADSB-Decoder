#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <coro/coro.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/oneshot.h>

/**
 * @file
 * Aircraft history in a SQLite file: every published aircraft update, written live, and read back as the
 * state of the sky at any past time (AircraftHistory::snapshot()) for the UI's time scrubber.
 *
 * One task on a blocking-pool thread owns the database and runs every write and query in turn, as requests
 * arrive on a channel. AircraftHistory is a handle to it: each request is a co_await that sends it, and for a
 * query waits for the reply. The task runs until every handle is gone, or until the runtime shuts down; it
 * writes the rows still queued before it stops, so stopping loses none that were sent.
 *
 * A query waits behind whatever is ahead of it, a long heatmap included, and so do the writes.
 */

/** One aircraft update to record. */
struct HistoryRow {
    double t_unix_s;  /**< AircraftState::last_seen_unix_s */
    uint32_t icao;
    std::optional<double> lat, lon;  /**< degrees, for finding tracks by index */
    std::string state;               /**< the aircraft as published (`{"type":"aircraft",...}`) */
};

/** The area, grid and time range of a heatmap; see AircraftHistory::heatmap(). */
struct HeatmapRequest {
    double west, south, east, north;  /**< degrees; east > west, north > south */
    int width, height;                /**< grid size in cells, clamped to 1..kHistoryHeatmapMaxCells */
    std::optional<double> t0, t1;     /**< Unix s; default the start and end of the database */
};

/**
 * A handle to the history task. For one task at a time, like the coro::MpscSender it wraps: give each task
 * its own clone(). Each query returns nullopt if it failed, with an ERROR logged, or if the history task has
 * stopped.
 */
class AircraftHistory {
public:
    /**
     * Opens (creating if needed) the database at `db_path` and starts the history task. Call from within the
     * coro runtime.
     * @return the task's first handle; nullopt, with an ERROR logged, if the database can't be opened
     */
    static std::optional<AircraftHistory> open(const std::string& db_path);

    /** Another handle to the same task. */
    AircraftHistory clone() const { return AircraftHistory(m_tx.clone()); }

    /**
     * Appends one block's aircraft updates, in one transaction. Dropped if the history task has stopped, which
     * it does only when the runtime shuts down.
     */
    coro::Coro<void> insert(std::vector<HistoryRow> rows);

    /**
     * The sky at `t_unix_s` (default: the latest update) as one
     * `{"type":"snapshot","t":...,"t_min":...,"t_max":...,"aircraft":[{"state":{...},"track":[[lat,lon,alt_ft],...]}]}`
     * message: each aircraft updated in the kHistorySnapshotWindowS before `t`, with its latest state at `t`
     * and its positions in the window, oldest first (alt_ft null where unknown). `t_min`/`t_max` span the
     * whole database (null when it's empty).
     */
    coro::Coro<std::optional<std::string>> snapshot(std::optional<double> t_unix_s);

    /**
     * One aircraft's recent history, for the UI's detail charts, as one
     * `{"type":"aircraft_history","icao":...,"t":...,"points":[[t,alt_ft,gs_kt,vr_fpm,signal_db,selected_alt_ft],...]}`
     * message: its updates in the kHistoryAircraftWindowS before `t_unix_s` (default: its latest update),
     * oldest first, with null for fields not yet known.
     * @param icao the aircraft's address, six lowercase hex digits
     */
    coro::Coro<std::optional<std::string>> aircraft(std::string icao, std::optional<double> t_unix_s);

    /**
     * How busy the sky was, for the UI's scrubber, as one
     * `{"type":"activity","bucket_s":60,"t0":...,"counts":[...]}` message: the number of distinct aircraft
     * updated in each kHistoryActivityBucketS bucket (aligned to the Unix epoch), consecutive from the one
     * starting at `t0`, 0 where none were. Covers the buckets from the one holding `from_unix_s` (default:
     * the whole database) to the latest; `t0` is null and `counts` empty when there are none. The last bucket
     * may still be filling, so ask again from its start for an update. About 15 ms per hour of history for
     * the whole database.
     */
    coro::Coro<std::optional<std::string>> activity(std::optional<double> from_unix_s);

    /**
     * Where aircraft flew, for the UI's map heatmap, as one
     * `{"type":"heatmap","west":...,"south":...,"east":...,"north":...,"width":...,"height":...,"t0":...,"t1":...,"max":...,"cells":[i,n,...]}`
     * message. The request's area is divided into a width x height grid, even in Web Mercator (so it overlays
     * a map as a stretched image), with row 0 at the north. `cells` lists the nonzero cells as pairs of
     * row-major index and count; `max` is the largest count.
     *
     * A cell's count is the number of flights that crossed it: each aircraft's consecutive positions less
     * than a minute apart are joined by a line, and every cell the line passes through counts that flight
     * once. A gap of 30 minutes or more starts a new flight. Counting flights rather than position updates
     * keeps aircraft near the receiver, heard more often, from dominating, and joining the positions fills
     * in the gaps between sparse ones far away.
     *
     * Reads every position in the time range: about 0.2 s per 200k positions.
     * @return the message, or nullopt also if the area is empty
     */
    coro::Coro<std::optional<std::string>> heatmap(HeatmapRequest req);

private:
    // Requests a handle can send. A query carries the sender for its reply.
    using Reply = coro::OneshotSender<std::optional<std::string>>;
    struct Insert {
        std::vector<HistoryRow> rows;
    };
    struct Snapshot {
        std::optional<double> t_unix_s;
        Reply reply;
    };
    struct Aircraft {
        std::string icao;
        std::optional<double> t_unix_s;
        Reply reply;
    };
    struct Activity {
        std::optional<double> from_unix_s;
        Reply reply;
    };
    struct Heatmap {
        HeatmapRequest req;
        Reply reply;
    };
    using Request = std::variant<Insert, Snapshot, Aircraft, Activity, Heatmap>;

    // Requests that can wait for the history task before a send waits too. One per block from the demod loop,
    // and at most one per WebSocket client, which waits for each reply before it asks again.
    static constexpr size_t kQueueRequests = 16;

    class Db;  // the connection, in aircraft_history.cpp

    explicit AircraftHistory(coro::MpscSender<Request> tx) : m_tx(std::move(tx)) {}
    // Sends `query` and waits for the reply that `result` receives.
    coro::Coro<std::optional<std::string>> ask(Request query,
                                               coro::OneshotReceiver<std::optional<std::string>> result);
    // The history task: handles each request in turn until every handle is gone or it is cancelled.
    static void run(coro::MpscReceiver<Request>& rx, Db& db);

    coro::MpscSender<Request> m_tx;
};
