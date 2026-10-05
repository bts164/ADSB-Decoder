#include "apps/vita49_send/sim_source.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <optional>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include <xtensor/xtensor.hpp>

#include "apps/vita49_send/vrt_encode.h"
#include "common/cpu_layout.h"
#include "common/log.h"

#include <coro/coro.h>
#include <coro/io/byte_buffer.h>
#include <coro/io/signal.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/sleep.h>
#include <coro/task/spawn_blocking.h>

namespace vita49_send {
namespace {

// UDP GSO limits: one send carries at most a maximal UDP payload and 64 segments (the kernel's
// UDP_MAX_SEGMENTS; 128 on newer kernels).
constexpr size_t kMaxGsoBytes = 65507;
constexpr size_t kMaxGsoSegments = 64;

// Equal-size VRT packets laid out back to back, sent with one UDP GSO send
// (UdpSocket::set_segment_size): the kernel splits the buffer into one datagram
// per packet. Only the first `used` bytes are sent.
struct PacketBatch {
    xt::xtensor<uint8_t, 1> bytes;
    size_t used = 0;

    uint8_t* begin() { return bytes.data(); }
    uint8_t* end() { return bytes.data() + used; }
    const uint8_t* begin() const { return bytes.data(); }
    const uint8_t* end() const { return bytes.data() + used; }
};
static_assert(coro::ByteBuffer<PacketBatch>);

coro::Coro<void> wait_for_sigint(std::shared_ptr<std::atomic<bool>> stop) {
    co_await coro::signal(SIGINT);
    LOGF(INFO, "stopping after SIGINT");
    stop->store(true);
}

// Reports the samples a paced sender dropped because it fell behind real time (see sim_send_loop's overruns).
// A receiver sees these as timestamp gaps but can't tell whether they were lost here or in the network, so the
// sender says so itself.
class PaceMonitor {
public:
    PaceMonitor(double rate_hz, std::chrono::steady_clock::time_point t0)
        : m_rate(rate_hz), m_t0(t0), m_window_start(t0) {}

    // `sent` samples are out and `overrun` were dropped so far. Reports once a second while dropping.
    void update(std::chrono::steady_clock::time_point now, uint64_t sent, uint64_t overrun) {
        const double window_s = std::chrono::duration<double>(now - m_window_start).count();
        if (window_s < 1.0) return;
        if (overrun > m_window_overrun) {
            const uint64_t lost = overrun - m_window_overrun;
            const uint64_t total = lost + sent - m_window_sent;
            LOGF(WARNING,
                 "fell behind real time; dropped %u samples (%.2f%%) over the last %.1f s, as a capture device "
                 "would on overrun",
                 lost, 100.0 * lost / std::max<uint64_t>(total, 1), window_s);
        }
        m_window_start = now;
        m_window_sent = sent;
        m_window_overrun = overrun;
    }

    // Logs the whole run's totals, as a warning if anything was dropped.
    void finish(std::chrono::steady_clock::time_point now, uint64_t sent, uint64_t overrun) const {
        const double elapsed_s = std::chrono::duration<double>(now - m_t0).count();
        if (elapsed_s <= 0) return;
        const double sent_rate = static_cast<double>(sent) / elapsed_s;
        LOGF(LEVEL(overrun > 0 ? absl::LogSeverity::kWarning : absl::LogSeverity::kInfo),
             "sent %u samples in %.2f s: %.1f Msps (%.1f%% of the %.1f Msps requested); %u samples (%.2f%%) "
             "dropped on overrun",
             sent, elapsed_s, sent_rate / 1e6, sent_rate / m_rate * 100, m_rate / 1e6, overrun,
             100.0 * overrun / std::max<uint64_t>(sent + overrun, 1));
    }

private:
    double m_rate;
    std::chrono::steady_clock::time_point m_t0;
    std::chrono::steady_clock::time_point m_window_start;
    uint64_t m_window_sent = 0;
    uint64_t m_window_overrun = 0;
};

// A sender that falls this far behind real time drops the late samples, like a capture device's FIFO overrun.
constexpr double kOverrunS = 100e-3;

// One step of the simulated stream: rendered ahead of time by sim_generate(), sent by sim_send_items() once due.
struct SimItem {
    std::optional<VrtPacket> context;           // IF Context packet, sent before the batch
    PacketBatch batch;                          // data packets (one GSO send)
    uint64_t samples = 0;                       // in batch
    uint64_t skipped = 0;                       // samples the generator dropped on overrun just before this item
    std::chrono::steady_clock::time_point due;  // wall time of the batch's last sample
};

// Renders the simulated stream into SimItems on its own thread and queues them for sim_send_items(). The queue's
// capacity is how far ahead of real time it may get; blocking_send() waits while the queue is full. Returns when
// the stream ends, `stop` is set, or the receiver is dropped. Owns everything it touches (see spawn_blocking()).
void sim_generate(std::unique_ptr<adsb_sim::Simulator> sim, SimStreamParams params, size_t batch_packets,
                  std::chrono::steady_clock::time_point t0, std::shared_ptr<std::atomic<bool>> stop,
                  coro::MpscSender<SimItem> tx, coro::MpscReceiver<PacketBatch> free_rx) {
    const double rate = params.sim.sample_rate_hz;
    PacketClock clock{params.sim.sample_rate_hz};
    ContextSender context(params.stream_id, StreamConfig{rate, params.rf_freq_hz, rate}, params.context_interval_s);
    const size_t spp = params.samples_per_packet;
    const size_t pkt_bytes = vrt_packet_bytes(spp);
    const auto samples_limit = params.duration_s > 0 ? static_cast<uint64_t>(params.duration_s * rate) : UINT64_MAX;
    // The simulator renders one batch at a time; the IQ content does not depend on the block size.
    std::vector<int16_t> block(2 * batch_packets * spp);
    uint32_t packet_count = 0;
    uint64_t pos = 0;      // stream position: samples rendered into items plus samples dropped on overrun
    uint64_t skipped = 0;  // dropped since the last item was queued
    auto due_at = [&](uint64_t samples) {
        return t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double>(static_cast<double>(samples) / rate));
    };
    // Reuses a buffer the sender has finished with, or allocates one while the pool is still filling up.
    auto new_item = [&] {
        SimItem item;
        if (auto buf = free_rx.try_recv()) item.batch = std::move(*buf);
        else item.batch.bytes = xt::xtensor<uint8_t, 1>::from_shape({batch_packets * pkt_bytes});
        item.batch.used = 0;
        item.skipped = std::exchange(skipped, 0);
        return item;
    };
    SimItem item = new_item();
    // Queues `item` (due when its last sample is) and starts the next; false once the sender has gone.
    auto flush = [&] {
        item.due = due_at(pos);
        const bool ok = tx.blocking_send(std::move(item)).has_value();
        item = new_item();
        return ok;
    };

    while (!stop->load() && pos < samples_limit) {
        // Packets left, rounded up; pos < samples_limit, so this can't overflow (samples_limit may be UINT64_MAX).
        const size_t n = static_cast<size_t>(std::min<uint64_t>(batch_packets, 1 + (samples_limit - pos - 1) / spp));
        sim->generate(std::span(block.data(), 2 * n * spp));
        for (size_t i = 0; i < n; i++) {
            if (auto ctx_pkt = context.next_if_due(clock)) {
                // Keep the stream in order: the data packets before it go out in an item of their own.
                if (item.samples > 0 && !flush()) return;
                item.context = std::move(*ctx_pkt);
            }
            write_vrt_packet_i16(item.batch.bytes.data() + item.batch.used, params.stream_id, packet_count,
                                 clock.next(spp), block.data() + 2 * i * spp, spp);
            item.batch.used += pkt_bytes;
            item.samples += spp;
            pos += spp;
            packet_count = (packet_count + 1) & 0xF;
        }
        if (!flush()) return;

        // Normally this thread is ahead of real time, waiting in blocking_send() for the sender to make room.
        // If it falls behind instead (too slow, or descheduled), what it renders next would be too late to send.
        const double late_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - due_at(pos)).count();
        if (late_s > kOverrunS) {
            // Overrun: drop whole packets covering the lag. The signal went on meanwhile, so render it (keeping
            // the IQ at every timestamp independent of pacing) and advance the timestamps and the packet
            // counter past it, so a receiver sees a gap, as it would from a device.
            uint64_t lost_packets = std::min<uint64_t>(static_cast<uint64_t>(late_s * rate) / spp,
                                                       (samples_limit - pos) / spp);
            pos += lost_packets * spp;
            skipped += lost_packets * spp;
            while (lost_packets > 0) {
                const size_t k = static_cast<size_t>(std::min<uint64_t>(lost_packets, batch_packets));
                sim->generate(std::span(block.data(), 2 * k * spp));
                for (size_t i = 0; i < k; i++) {
                    clock.next(spp);
                    packet_count = (packet_count + 1) & 0xF;
                }
                lost_packets -= k;
            }
        }
    }
    // tx is dropped here, which ends sim_send_items() once it has sent what is queued.
}

// Sends each queued item once the wall clock reaches its last sample, then hands its buffer back to the
// generator. An item more than kOverrunS late (this thread was stalled that long) is dropped, and counted, like
// a FIFO overrun. Returns when the generator is done or `stop` is set; dropping `rx` then releases a generator
// waiting for queue space.
coro::Coro<void> sim_send_items(coro::UdpSocket& sock, coro::MpscReceiver<SimItem> rx_param,
                                coro::MpscSender<PacketBatch> free_tx, std::shared_ptr<std::atomic<bool>> stop,
                                PaceMonitor& pace, uint64_t& sent, uint64_t& overrun) {
    // A body local, not the parameter: locals are destroyed as the body exits, parameters only with the frame.
    coro::MpscReceiver<SimItem> rx = std::move(rx_param);
    while (!stop->load()) {
        auto item = co_await coro::next(rx);
        if (!item) break;
        overrun += item->skipped;
        // Coro timers bound the IoDriver's epoll_pwait2 at nanosecond resolution, fine enough for a batch.
        // Already due (the sender is behind): ready at once, no timer queued.
        co_await coro::sleep_until(item->due);
        const auto now = std::chrono::steady_clock::now();
        // Context packets are tiny and repeated; send them even when late.
        if (item->context) co_await sock.send(std::move(*item->context));
        if (std::chrono::duration<double>(now - item->due).count() > kOverrunS) {
            overrun += item->samples;
        } else {
            item->batch = co_await sock.send(std::move(item->batch));
            sent += item->samples;
        }
        // Best effort: if the pool is full (or the generator has finished), the buffer is just freed.
        (void)free_tx.try_send(std::move(item->batch));
        pace.update(now, sent, overrun);
    }
}

}  // namespace

coro::Coro<void> sim_send_loop(coro::UdpSocket sock, std::unique_ptr<adsb_sim::Simulator> sim, SimStreamParams params) {
    const double rate = params.sim.sample_rate_hz;
    const size_t spp = params.samples_per_packet;

    // Data packets go out up to 44 (of 1468 B) per send via UDP GSO: the kernel's per-datagram work, not the
    // syscall, is what limits a small-datagram sender, and GSO does that work once per send. See the
    // PERFORMANCE NOTE in main.cpp's main().
    const size_t pkt_bytes = vrt_packet_bytes(spp);
    size_t batch_packets = std::clamp<size_t>(kMaxGsoBytes / pkt_bytes, 1, kMaxGsoSegments);
    if (batch_packets > 1) {
        try {
            sock.set_segment_size(pkt_bytes);
        } catch (const std::system_error& err) {
            LOGF(WARNING, "UDP GSO unavailable (%s), sending one packet per send", err.what());
            batch_packets = 1;
        }
    }

    // Queue depth in items. Items split at context packets hold fewer samples, so the real lead is slightly
    // less than queue_s. A deep queue means every send reads a cache-cold buffer (see the PERFORMANCE NOTE's
    // channel depth); --queue-ms sets the tradeoff.
    const auto batch_samples = static_cast<double>(batch_packets * spp);
    const auto queue_items = static_cast<size_t>(std::max(1.0, std::ceil(params.queue_s * rate / batch_samples)));
    LOGF(INFO, "render queue: %u batches of %u packets (%.1f ms, %.1f MB)", queue_items, batch_packets,
         queue_items * batch_samples / rate * 1e3, queue_items * batch_packets * pkt_bytes / 1e6);
    auto [tx, rx] = coro::mpsc_channel<SimItem>(queue_items);
    // Buffers come back for reuse; a couple more than the queue covers the item each stage holds.
    auto [free_tx, free_rx] = coro::mpsc_channel<PacketBatch>(queue_items + 2);

    auto stop = std::make_shared<std::atomic<bool>>(false);
    auto sigint_watcher = coro::spawn(wait_for_sigint(stop));

    const auto t0 = std::chrono::steady_clock::now();
    auto generator = coro::spawn_blocking(
        [sim = std::move(sim), params, batch_packets, t0, stop, tx = std::move(tx),
         free_rx = std::move(free_rx)]() mutable {
            // Rendering is compute, not IO: it keeps the default slice rather than the IO threads' short one.
            adsb::cpu_layout::set_short_slice(false);
            sim_generate(std::move(sim), std::move(params), batch_packets, t0, std::move(stop), std::move(tx),
                         std::move(free_rx));
        });

    PaceMonitor pace(rate, t0);
    uint64_t sent = 0;
    uint64_t overrun = 0;
    co_await sim_send_items(sock, std::move(rx), std::move(free_tx), stop, pace, sent, overrun);
    co_await generator;
    pace.finish(std::chrono::steady_clock::now(), sent, overrun);
}

}  // namespace vita49_send
