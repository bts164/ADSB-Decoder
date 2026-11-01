#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <coro/coro.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/oneshot.h>
#include <coro/sync/watch.h>

#include "api/convert.h"

/**
 * @file
 * Aircraft history in a SQLite file: every published aircraft update, written live, and read back as the
 * state of the sky at any past time (AircraftHistory::snapshot()) for the UI's time scrubber. Beside them, the
 * receiver's position over time (record_receiver_positions()). Each query
 * takes and returns the UI API's messages (proto/adsb/api/v1/api.proto).
 *
 * One task on a blocking-pool thread owns the database and runs every write and query in turn, as requests
 * arrive on a channel. AircraftHistory is a handle to it: each request is a co_await that sends it, and for a
 * query waits for the reply. The task runs until every handle is gone, or until the runtime shuts down; it
 * writes the rows still queued before it stops, so stopping loses none that were sent.
 *
 * A query waits behind whatever is ahead of it, a long heatmap included, and so do the writes.
 *
 * The file records its schema's version. A version 2 file, from before the receiver's positions, gains their
 * table; a file of any other version is refused rather than misread.
 */

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
     * @return the task's first handle; nullopt, with an ERROR logged, if the database can't be opened or was
     *     written by another version of adsb
     */
    static std::optional<AircraftHistory> open(const std::string& db_path);

    /** Another handle to the same task. */
    AircraftHistory clone() const { return AircraftHistory(m_tx.clone()); }

    /**
     * Appends one block's aircraft updates, each as published, in one transaction. Dropped if the history task
     * has stopped, which it does only when the runtime shuts down.
     */
    coro::Coro<void> insert(std::vector<api::Aircraft> rows);

    /** Appends one row of the receiver's position, at `row.t`, replacing one at the same time. */
    coro::Coro<void> insert_position(api::ReceiverPosition row);

    /**
     * The sky at `req.t` (default: the latest update): each aircraft updated in the kHistorySnapshotWindowS
     * before `t`, with its latest state at `t` and its positions in the window, oldest first. `t_min`/`t_max`
     * span the whole database, and are unset when it's empty. `receiver` is the receiver's position at `t`: the
     * latest row at or before it, if less than kPositionHoldsS old, and otherwise unset.
     */
    [[nodiscard]] coro::Coro<std::optional<api::Snapshot>> snapshot(api::GetSnapshotRequest req);

    /**
     * One aircraft's recent history, for the UI's detail charts: its updates in the kHistoryAircraftWindowS
     * before `req.t` (default: its latest update), oldest first, each field unset where not yet known.
     */
    [[nodiscard]] coro::Coro<std::optional<api::AircraftHistory>> aircraft(api::GetAircraftHistoryRequest req);

    /**
     * How busy the sky was, for the UI's scrubber: the number of distinct aircraft updated in each
     * kHistoryActivityBucketS bucket (aligned to the Unix epoch), consecutive from the one starting at `t0`, 0
     * where none were. Covers the buckets from the one holding `req.from` (default: the whole database) to the
     * latest; `t0` is unset and `counts` empty when there are none. The last bucket may still be filling, so
     * ask again from its start for an update. About 15 ms per hour of history for the whole database.
     */
    [[nodiscard]] coro::Coro<std::optional<api::Activity>> activity(api::GetActivityRequest req);

    /**
     * Where aircraft flew, for the UI's map heatmap. The request's area (degrees; east > west, north > south)
     * is divided into a width x height grid, each side clamped to 1..kHistoryHeatmapMaxCells, even in Web
     * Mercator (so it overlays a map as a stretched image), with row 0 at the north. `t0`/`t1` default to the
     * start and end of the database. The reply lists the nonzero cells, each as its row-major index and
     * count; `max` is the largest count.
     *
     * A cell's count is the number of flights that crossed it: each aircraft's consecutive positions less
     * than a minute apart are joined by a line, and every cell the line passes through counts that flight
     * once. A gap of 30 minutes or more starts a new flight. Counting flights rather than position updates
     * keeps aircraft near the receiver, heard more often, from dominating, and joining the positions fills
     * in the gaps between sparse ones far away.
     *
     * Reads every position in the time range: about 0.2 s per 200k positions.
     * @return the heatmap, or nullopt also if the area is empty
     */
    [[nodiscard]] coro::Coro<std::optional<api::Heatmap>> heatmap(api::GetHeatmapRequest req);

    /**
     * The receiver's positions from `req.from` to `req.to` (default: the whole database), oldest first, with the
     * row in effect at `from` first, if any. Each row's `summary` is empty: the table doesn't keep it.
     */
    [[nodiscard]] coro::Coro<std::optional<api::ReceiverPositions>> positions(api::GetReceiverPositionsRequest req);

    /** How long a position row holds, with no row after it: the recorder writes one a minute. */
    static constexpr double kPositionHoldsS = 90;

private:
    // Requests a handle can send. A query carries the sender for its reply.
    template <typename T>
    using Reply = coro::OneshotSender<std::optional<T>>;
    struct Insert {
        std::vector<api::Aircraft> rows;
    };
    struct Snapshot {
        api::GetSnapshotRequest req;
        Reply<api::Snapshot> reply;
    };
    struct Aircraft {
        api::GetAircraftHistoryRequest req;
        Reply<api::AircraftHistory> reply;
    };
    struct Activity {
        api::GetActivityRequest req;
        Reply<api::Activity> reply;
    };
    struct Heatmap {
        api::GetHeatmapRequest req;
        Reply<api::Heatmap> reply;
    };
    struct InsertPosition {
        api::ReceiverPosition row;
    };
    struct Positions {
        api::GetReceiverPositionsRequest req;
        Reply<api::ReceiverPositions> reply;
    };
    using Request = std::variant<Insert, Snapshot, Aircraft, Activity, Heatmap, InsertPosition, Positions>;

    // Requests that can wait for the history task before a send waits too. One per block from the demod loop,
    // and a few per WebSocket client (wsrpc::Limits::client_calls).
    static constexpr size_t kQueueRequests = 16;

    class Db;  // the connection, in aircraft_history.cpp

    explicit AircraftHistory(coro::MpscSender<Request> tx) : m_tx(std::move(tx)) {}
    // Sends the query `Query{req}` and waits for its reply.
    template <typename Query, typename Result, typename Req>
    coro::Coro<std::optional<Result>> ask(Req req);
    // The history task: handles each request in turn until every handle is gone or it is cancelled.
    static void run(coro::MpscReceiver<Request>& rx, Db& db);

    coro::MpscSender<Request> m_tx;
};

/**
 * Records the receiver's position from `position` into `history` until cancelled: a row at once, then one on a
 * change of GPS state, on a move of more than 25 m, and otherwise once a minute, so a time with no row in the
 * 90 s before it is one nothing was recording. Each row is stamped with the system clock when written.
 */
[[nodiscard]] coro::Coro<void> record_receiver_positions(coro::WatchReceiver<api::ReceiverPosition> position,
                                                         AircraftHistory history);
