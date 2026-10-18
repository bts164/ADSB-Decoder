#include "apps/vita49_send/pace_stats.h"

#include <algorithm>

#include "common/log.h"

#include <coro/sync/interval.h>
#include <coro/sync/join.h>

using namespace std::chrono;
using namespace std::chrono_literals;

coro::Coro<void> pace_monitor_loop(coro::WatchReceiver<PaceStats> pace_rx) {
    coro::IntervalTimer timer(1s);
    PaceStats last = pace_rx.get();
    auto last_t = steady_clock::now();
    while (true) {
        // Wait for both the stats to change and the interval to expire: if the stats haven't changed, there
        // is nothing to check.
        auto [changed, tick] = co_await coro::join(pace_rx.changed(), timer.tick());
        (void)tick;
        if (!changed) co_return;  // senders dropped: the run is over
        const PaceStats next = pace_rx.get();
        const auto now = steady_clock::now();
        if (next.overrun() > last.overrun()) {
            const uint64_t lost = next.overrun() - last.overrun();
            const uint64_t total = lost + next.sent - last.sent;
            LOGF(WARNING,
                 "fell behind real time; dropped %u samples (%.2f%%) over the last %.1f s, as a capture device "
                 "would on overrun: %u render too slow, %u send queue full, %u sent too late; %.0f ms spent in "
                 "socket sends",
                 lost, 100.0 * lost / std::max<uint64_t>(total, 1), duration<double>(now - last_t).count(),
                 next.overrun_render - last.overrun_render, next.overrun_queue_full - last.overrun_queue_full,
                 next.overrun_late_send - last.overrun_late_send,
                 duration<double, std::milli>(next.send_wait - last.send_wait).count());
        }
        last = next;
        last_t = now;
    }
}
