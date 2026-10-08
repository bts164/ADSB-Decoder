#include "apps/vita49_send/sim_source.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "apps/vita49_send/packet_sender.h"
#include "apps/vita49_send/vrt_encode.h"
#include "common/cpu_layout.h"
#include "common/log.h"

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/io/byte_buffer.h>
#include <coro/io/signal.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/sleep.h>
#include <coro/sync/timeout.h>
#include <coro/sync/watch.h>
#include <coro/task/spawn_blocking.h>

namespace vita49_send {
namespace {

using namespace std::chrono;
using namespace std::chrono_literals;

coro::Coro<void> wait_for_sigint(std::shared_ptr<std::atomic<bool>> stop) {
    co_await coro::signal(SIGINT);
    LOGF(INFO, "stopping after SIGINT");
    stop->store(true);
}

// One step of the simulated stream: rendered ahead of time by sim_generate(), released by sim_packet_stream()
// once due.
struct SimItem {
    std::optional<VrtPacket> context;  // IF Context packet, sent before the batch
    PacketBatch batch;                 // data packets (one GSO send)
    steady_clock::time_point due;      // wall time of the batch's last sample
};

// Renders the simulated stream into SimItems on its own thread and queues them for sim_packet_stream(). The
// queue's capacity is how far ahead of real time it may get; flush() waits while the queue is full. Returns
// when the stream ends, `stop` is set, or the receiver is dropped. Owns everything it touches (see
// spawn_blocking()).
void sim_generate(std::unique_ptr<adsb_sim::Simulator> sim, SimStreamParams params, steady_clock::time_point t0,
                  std::shared_ptr<std::atomic<bool>> stop, coro::MpscSender<SimItem> tx,
                  coro::MpscReceiver<BatchBytes> free_rx, BatchPool pool, coro::WatchSender<PaceStats> paceTx) {
    const size_t batch_packets = params.batch_packets;
    const double rate = params.sim.sample_rate_hz;
    PacketClock clock{params.sim.sample_rate_hz};
    ContextSender context(params.stream_id, StreamConfig{rate, params.rf_freq_hz, rate}, params.context_interval_s);
    const size_t spp = params.samples_per_packet;
    const size_t pkt_bytes = vrt_packet_bytes(spp);
    const auto samples_limit = params.duration_s > 0 ? static_cast<uint64_t>(params.duration_s * rate) : UINT64_MAX;
    // The simulator renders a whole number of batches at a time: one, or with several render threads as many
    // as fit in the block it splits between them (see Simulator::block_samples()), so that each thread has
    // enough to do per call. The IQ content does not depend on the block size.
    const size_t render_batches =
        params.sim.render_threads > 1 ? std::max<size_t>(1, sim->block_samples() / (batch_packets * spp)) : 1;
    const size_t render_packets = batch_packets * render_batches;
    std::vector<int16_t> block(2 * render_packets * spp);
    uint32_t packet_count = 0;
    uint64_t pos = 0;  // stream position: samples rendered into items plus samples dropped on overrun
    auto due_at = [&](uint64_t samples) {
        return t0 + duration_cast<steady_clock::duration>(
                        duration<double>(static_cast<double>(samples) / rate));
    };
    // Reuses a buffer that has been sent, or allocates one while the pool is still filling up.
    auto new_item = [&] {
        SimItem item;
        if (auto buf = free_rx.try_recv()) {
            item.batch.bytes = std::move(*buf);
        } else {
            item.batch.bytes = BatchBytes::from_shape({batch_packets * pkt_bytes});
        }
        item.batch.pool = pool;
        return item;
    };
    SimItem item = new_item();
    // Queues `item` (due when its last sample is) and starts the next; false once the consumer has gone.
    // Waits for queue space only until the item is kOverrunS late. Past that the consumer is stuck and the
    // item would be dropped as an overrun anyway, so it is dropped here, counted as overrun_queue_full, and
    // rendering carries on. The generator thus keeps up with real time through a stall instead of having
    // the whole gap to render once the consumer is back.
    auto flush = [&] {
        item.due = due_at(pos);
        const auto deadline = time_point_cast<steady_clock::duration>(item.due + duration<double>(kOverrunS));
        const uint64_t samples = item.batch.samples;
        auto result = coro::blocking_wait(coro::timeout_at(deadline, tx.send(std::move(item))));
        // On a timeout the item was destroyed with the send future, which put its buffer back in the pool.
        item = new_item();
        if (result.index() == 0) return std::get<0>(result).value.has_value();
        paceTx.send_if_modified([&](PaceStats& stats) {
            stats.overrun_queue_full += samples;
            return true;
        });
        return true;
    };

    while (!stop->load() && pos < samples_limit) {
        // Packets left, rounded up; pos < samples_limit, so this can't overflow (samples_limit may be UINT64_MAX).
        const size_t n = static_cast<size_t>(std::min<uint64_t>(render_packets, 1 + (samples_limit - pos - 1) / spp));
        sim->generate(std::span(block.data(), 2 * n * spp));
        for (size_t i = 0; i < n; i++) {
            if (auto ctx_pkt = context.next_if_due(clock)) {
                // Keep the stream in order: the data packets before it go out in an item of their own.
                if (item.batch.samples > 0 && !flush()) {
                    return;
                }
                item.context = std::move(*ctx_pkt);
            }
            write_vrt_packet_i16(item.batch.bytes.data() + item.batch.used, params.stream_id, packet_count,
                                 clock.next(spp), block.data() + 2 * i * spp, spp);
            item.batch.used += pkt_bytes;
            item.batch.samples += spp;
            pos += spp;
            packet_count = (packet_count + 1) & 0xF;
            if (item.batch.used == batch_packets * pkt_bytes && !flush()) {
                return;
            }
        }
        if (item.batch.samples > 0 && !flush()) {
            return;
        }

        // Normally this thread is ahead of real time, waiting in flush() for the sender to make room.
        // If it falls behind instead (too slow, or descheduled), what it renders next would be too late to send.
        const double late_s = duration<double>(steady_clock::now() - due_at(pos)).count();
        if (late_s > kOverrunS) {
            // Overrun: drop whole packets covering the lag. The signal went on meanwhile, so render it (keeping
            // the IQ at every timestamp independent of pacing) and advance the timestamps and the packet
            // counter past it, so a receiver sees a gap, as it would from a device.
            uint64_t lost_packets = std::min<uint64_t>(static_cast<uint64_t>(late_s * rate) / spp,
                                                       (samples_limit - pos) / spp);
            pos += lost_packets * spp;
            paceTx.send_if_modified([&](PaceStats& stats) {
                stats.overrun_render += lost_packets * spp;
                return lost_packets > 0;
            });
            while (lost_packets > 0) {
                const size_t k = static_cast<size_t>(std::min<uint64_t>(lost_packets, render_packets));
                sim->generate(std::span(block.data(), 2 * k * spp));
                for (size_t i = 0; i < k; i++) {
                    clock.next(spp);
                    packet_count = (packet_count + 1) & 0xF;
                }
                lost_packets -= k;
            }
        }
    }
    // tx is dropped here, which ends sim_packet_stream() once it has released what is queued.
}

}  // namespace

coro::CoroStream<PacketBatch> sim_packet_stream(std::unique_ptr<adsb_sim::Simulator> sim, SimStreamParams params,
                                                coro::WatchSender<PaceStats> paceTx) {
    const double rate = params.sim.sample_rate_hz;
    const size_t spp = params.samples_per_packet;
    const size_t pkt_bytes = vrt_packet_bytes(spp);
    const size_t batch_packets = params.batch_packets;

    // Queue depth in items. Items split at context packets hold fewer samples, so the real lead is slightly
    // less than queue_s. A deep queue means every send reads a cache-cold buffer (see the PERFORMANCE NOTE's
    // channel depth); --queue-ms sets the tradeoff.
    const auto batch_samples = static_cast<double>(batch_packets * spp);
    const auto queue_items = static_cast<size_t>(std::max(1.0, std::ceil(params.queue_s * rate / batch_samples)));
    LOGF(INFO, "render queue: %u batches of %u packets (%.1f ms, %.1f MB)", queue_items, batch_packets,
         queue_items * batch_samples / rate * 1e3, queue_items * batch_packets * pkt_bytes / 1e6);
    auto [tx, rx] = coro::mpsc_channel<SimItem>(queue_items);
    // Buffers come back for reuse (see PacketBatch); a couple more than the queue covers the item each stage
    // holds.
    auto [free_tx, free_rx] = coro::mpsc_channel<BatchBytes>(queue_items + 2);
    BatchPool pool = std::make_shared<coro::MpscSender<BatchBytes>>(std::move(free_tx));

    auto stop = std::make_shared<std::atomic<bool>>(false);
    auto sigint_watcher = coro::spawn(wait_for_sigint(stop));

    const auto t0 = steady_clock::now();
    auto generator = coro::spawn_blocking(
        [sim = std::move(sim), params, t0, stop, tx = std::move(tx), free_rx = std::move(free_rx),
         pool = std::move(pool), paceTx = paceTx.clone()]() mutable {
            // Rendering is compute, not IO: it keeps the default slice rather than the IO threads' short one.
            adsb::cpu_layout::set_short_slice(false);
            sim_generate(std::move(sim), std::move(params), t0, std::move(stop), std::move(tx), std::move(free_rx),
                         std::move(pool), std::move(paceTx));
        });

    {
        // Scoped, so that leaving the loop drops the receiver and releases a generator waiting for queue space.
        coro::MpscReceiver<SimItem> queue = std::move(rx);
        uint64_t late = 0;  // samples dropped here
        while (!stop->load()) {
            auto item = co_await coro::next(queue);
            if (!item) break;
            // Hold each batch back until the wall clock reaches its last sample. Coro timers bound the
            // IoDriver's epoll_pwait2 at nanosecond resolution, fine enough for a batch. Already due (the
            // consumer is behind): ready at once, no timer queued.
            co_await coro::sleep_until(item->due);
            const bool too_late = duration<double>(steady_clock::now() - item->due).count() > kOverrunS;
            // Context packets are tiny and repeated; they go out even when late.
            if (item->context) co_yield single_packet_batch(std::move(*item->context));
            if (too_late) {
                // The consumer was stalled that long: drop the batch, like a FIFO overrun. Its buffer goes
                // back to the pool as the item is destroyed.
                late += item->batch.samples;
                paceTx.send_if_modified([&](PaceStats& stats) {
                    stats.overrun_late_send = late;
                    return true;
                });
                continue;
            }
            co_yield std::move(item->batch);
        }
    }
    co_await generator;
}

}  // namespace vita49_send
