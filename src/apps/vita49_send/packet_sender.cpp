#include "apps/vita49_send/packet_sender.h"

#include <algorithm>
#include <system_error>
#include <utility>

#include "common/log.h"

#include <coro/runtime/runtime.h>
#include <coro/sync/interval.h>
#include <coro/sync/join.h>

namespace vita49_send {

using namespace std::chrono;
using namespace std::chrono_literals;

namespace {

// Logs a whole run's totals, as a warning if anything was dropped.
void log_pace_summary(const PaceStats& stats, double elapsed_s, double rate_hz) {
    if (elapsed_s <= 0) return;
    const double sent_rate = static_cast<double>(stats.sent) / elapsed_s;
    const uint64_t overrun = stats.overrun();
    LOGF(LEVEL(overrun > 0 ? absl::LogSeverity::kWarning : absl::LogSeverity::kInfo),
         "sent %u samples in %.2f s: %.1f Msps (%.1f%% of the %.1f Msps requested); %u samples (%.2f%%) "
         "dropped on overrun (%u render too slow, %u send queue full, %u sent too late); %.2f s spent in socket "
         "sends",
         stats.sent, elapsed_s, sent_rate / 1e6, sent_rate / rate_hz * 100, rate_hz / 1e6, overrun,
         100.0 * overrun / std::max<uint64_t>(stats.sent + overrun, 1), stats.overrun_render,
         stats.overrun_queue_full, stats.overrun_late_send, duration<double>(stats.send_wait).count());
}

}  // namespace

size_t enable_gso(coro::UdpSocket& sock, size_t pkt_bytes) {
    const size_t batch_packets = std::clamp<size_t>(kMaxGsoBytes / pkt_bytes, 1, kMaxGsoSegments);
    if (batch_packets == 1) return 1;
    try {
        sock.set_segment_size(pkt_bytes);
    } catch (const std::system_error& err) {
        LOGF(WARNING, "UDP GSO unavailable (%s), sending one packet per send", err.what());
        return 1;
    }
    return batch_packets;
}

coro::Coro<void> send_loop(coro::UdpSocket sock, coro::CoroStream<PacketBatch> stream, double rate_hz,
                           coro::WatchSender<PaceStats> paceTx, coro::WatchReceiver<PaceStats> paceRx) {
    // Cancelled when this coroutine returns.
    auto pace_monitor = coro::spawn(paceMonitorLoop(paceRx.clone()));
    const auto t0 = steady_clock::now();
    uint64_t sent = 0;
    steady_clock::duration send_wait{};
    while (auto batch = co_await coro::next(stream)) {
        const uint64_t samples = batch->samples;
        const auto send_start = steady_clock::now();
        // The batch that comes back is dropped here, which returns its buffer to the source's pool.
        co_await sock.send(std::move(*batch));
        send_wait += steady_clock::now() - send_start;
        sent += samples;
        paceTx.send_if_modified([&](PaceStats& stats) {
            stats.sent = sent;
            stats.send_wait = send_wait;
            return true;
        });
    }
    log_pace_summary(paceRx.get(), duration<double>(steady_clock::now() - t0).count(), rate_hz);
}

coro::Coro<void> paceMonitorLoop(coro::WatchReceiver<PaceStats> paceRx) {
    coro::IntervalTimer timer(1s);
    PaceStats last = paceRx.get();
    auto last_t = steady_clock::now();
    while (true) {
        // Wait for both the stats to change and the interval to expire: if the stats haven't changed, there
        // is nothing to check.
        auto [changed, tick] = co_await coro::join(paceRx.changed(), timer.tick());
        (void)tick;
        if (!changed) co_return;  // senders dropped: the run is over
        const PaceStats next = paceRx.get();
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

}  // namespace vita49_send
