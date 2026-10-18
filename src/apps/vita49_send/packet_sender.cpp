#include "apps/vita49_send/packet_sender.h"

#include <algorithm>
#include <utility>

#include "common/log.h"
#include "common/trace.h"

#include <coro/runtime/runtime.h>

using namespace std::chrono;

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

coro::Coro<void> send_loop(coro::UdpSocket sock, coro::CoroStream<PacketBatch> stream, double rate_hz,
                           coro::WatchSender<PaceStats> pace_tx, coro::WatchReceiver<PaceStats> pace_rx) {
    // Cancelled when this coroutine returns.
    auto pace_monitor = coro::build_task().name("pace_monitor").spawn(pace_monitor_loop(pace_rx.clone()));
    const auto t0 = steady_clock::now();
    // Logged however the loop ends: at the end of the stream, or when main() cancels this coroutine on Ctrl+C
    // (the frame is then destroyed where it is suspended, so only a destructor still runs).
    struct Summary {
        coro::WatchReceiver<PaceStats>& pace_rx;
        steady_clock::time_point t0;
        double rate_hz;
        ~Summary() { log_pace_summary(pace_rx.get(), duration<double>(steady_clock::now() - t0).count(), rate_hz); }
    } summary{pace_rx, t0, rate_hz};
    VLOGF(1, "sending at %.3f Msps", rate_hz / 1e6);
    uint64_t sent = 0;
    steady_clock::duration send_wait{};
    while (auto batch = co_await coro::next(stream)) {
        const uint64_t samples = batch->samples;
        const size_t bytes = batch->datagrams.used;
        const auto send_start = steady_clock::now();
        // The batch that comes back is dropped here, which returns its buffer to the source's pool.
        co_await sock.send(std::move(batch->datagrams));
        const auto send_time = steady_clock::now() - send_start;
        send_wait += send_time;
        TRACE_PLOT("udp send us", duration<double, std::micro>(send_time).count());
        VLOGF(2, "sent %u samples (%u bytes) in %.1f us", samples, bytes,
              duration<double, std::micro>(send_time).count());
        sent += samples;
        pace_tx.send_if_modified([&](PaceStats& stats) {
            stats.sent = sent;
            stats.send_wait = send_wait;
            return true;
        });
    }
    VLOGF(1, "stream ended after %.2f s", duration<double>(steady_clock::now() - t0).count());
}
