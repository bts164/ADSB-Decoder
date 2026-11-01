#include "apps/vita49_send/pace.h"

#include <chrono>
#include <utility>

#include "common/log.h"

#include <coro/coro.h>
#include <coro/sync/sleep.h>

using namespace std::chrono;

coro::CoroStream<PacketBatch> pace(coro::CoroStream<PacketBatch> stream, double rate_hz,
                                   coro::WatchSender<PaceStats> pace_tx) {
    steady_clock::time_point t0;
    uint64_t samples = 0;  // the stream's, through the current batch, dropped ones included
    while (auto batch = co_await coro::next(stream)) {
        if (samples == 0) t0 = steady_clock::now();
        if (batch->samples == 0) {
            co_yield std::move(*batch);
            continue;
        }
        samples += batch->samples;
        const auto due =
            t0 + duration_cast<steady_clock::duration>(duration<double>(static_cast<double>(samples) / rate_hz));
        co_await coro::sleep_until(due);
        if (duration<double>(steady_clock::now() - due).count() > PaceStats::kOverrunS) {
            VLOGF(2, "pace: sender asked for a batch more than %g ms after it was due; dropping %u samples",
                  PaceStats::kOverrunS * 1e3, batch->samples);
            count_dropped(pace_tx, &PaceStats::overrun_late_send, batch->samples);
            continue;
        }
        co_yield std::move(*batch);
    }
}
