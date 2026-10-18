#pragma once

// vita49_send's pacing statistics: how many samples a run sent, and how many it dropped because a stage fell
// behind real time.

#include <chrono>
#include <cstdint>

#include <coro/coro.h>
#include <coro/sync/watch.h>

#include "common/trace.h"

// Running totals of a run, in samples, on a watch channel. main() creates it; the source and send_loop() each
// publish their own fields through their own WatchSender. A source that never drops needs no WatchSender.
// The three overrun counts say which stage of the source dropped the samples:
struct PaceStats {
    // A stage that falls this far behind real time drops the late samples, like a capture device's FIFO
    // overrun.
    static constexpr double kOverrunS = 100e-3;

    uint64_t sent = 0;
    // The source was more than kOverrunS behind real time producing samples (the sim source: rendering is too
    // slow, or its thread was descheduled).
    uint64_t overrun_render = 0;
    // The source dropped a batch it could not queue: the sender is not draining the stream.
    uint64_t overrun_queue_full = 0;
    // A paced source was asked for a batch more than kOverrunS after it was due.
    uint64_t overrun_late_send = 0;
    // Time send_loop() spent awaiting socket sends. When the sender is the stage behind, this tells a socket
    // that isn't taking data (most of the window) from a send task that isn't getting to run (little of it).
    std::chrono::steady_clock::duration send_wait{};

    uint64_t overrun() const { return overrun_render + overrun_queue_full + overrun_late_send; }
};

// Adds `samples` to one of the overrun counts, e.g. count_dropped(pace_tx, &PaceStats::overrun_render, n). Every
// drop is counted this way, so each count is a running total that only its own stage adds to.
inline void count_dropped(coro::WatchSender<PaceStats>& pace_tx, uint64_t PaceStats::*overrun, uint64_t samples) {
    pace_tx.send_if_modified([&](PaceStats& stats) {
        stats.*overrun += samples;
        TRACE_PLOT("samples dropped", static_cast<int64_t>(stats.overrun()));
        return true;
    });
}

// Reports the samples a run dropped because a stage fell behind real time. A receiver sees these as timestamp
// gaps but can't tell whether they were lost here or in the network, so the sender says so itself. Runs as its
// own task, so it reports while the send loop is stuck (the source goes on publishing what it drops); reports
// once a second while dropping, and returns once every publisher has gone.
coro::Coro<void> pace_monitor_loop(coro::WatchReceiver<PaceStats> pace_rx);
