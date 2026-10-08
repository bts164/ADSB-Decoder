#pragma once

// vita49_send's send stage. A source is a coro::CoroStream<PacketBatch>: main() builds one from the command
// line and hands it to send_loop(), which forwards each batch to the UdpSocket and knows nothing else about
// where the batches come from. How many packets go in a batch, when a batch is released (pacing) and what
// is dropped when the source falls behind are all the source's business.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <xtensor/xtensor.hpp>

#include "apps/vita49_send/vrt_encode.h"

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/io/byte_buffer.h>
#include <coro/io/udp_socket.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/watch.h>

namespace vita49_send {

// UDP GSO limits: one send carries at most a maximal UDP payload and 64 segments (the kernel's
// UDP_MAX_SEGMENTS; 128 on newer kernels).
constexpr size_t kMaxGsoBytes = 65507;
constexpr size_t kMaxGsoSegments = 64;

using BatchBytes = xt::xtensor<uint8_t, 1>;

// Where a source's batch buffers go back to once sent, so the source can reuse them. Shared by every batch
// of the source.
using BatchPool = std::shared_ptr<coro::MpscSender<BatchBytes>>;

// VRT packets laid out back to back, sent with one send. Only the first `used` bytes are sent. More than one
// packet needs UDP GSO (enable_gso()): the kernel splits the buffer into one datagram per packet, so the
// packets must be the same size, except that the last may be shorter.
//
// Move-only. A batch with a `pool` hands its buffer back to it when destroyed (best effort: if the pool is
// full or the source has gone, the buffer is just freed), so whoever ends up with the batch recycles it by
// dropping it.
struct PacketBatch {
    BatchBytes bytes;
    size_t used = 0;
    uint64_t samples = 0;  // I/Q samples in the batch, for the statistics; 0 for a context packet
    BatchPool pool;

    PacketBatch() = default;
    PacketBatch(PacketBatch&&) noexcept = default;
    PacketBatch& operator=(PacketBatch&& other) noexcept {
        if (this != &other) {
            recycle();
            bytes = std::move(other.bytes);
            used = other.used;
            samples = other.samples;
            pool = std::move(other.pool);
        }
        return *this;
    }
    ~PacketBatch() { recycle(); }

    uint8_t* begin() { return bytes.data(); }
    uint8_t* end() { return bytes.data() + used; }
    const uint8_t* begin() const { return bytes.data(); }
    const uint8_t* end() const { return bytes.data() + used; }

private:
    void recycle() noexcept {
        // A moved-from batch has no pool (moving a shared_ptr nulls it), so its buffer isn't sent back.
        if (pool) (void)pool->try_send(std::move(bytes));
        pool.reset();
    }
};
static_assert(coro::ByteBuffer<PacketBatch>);

// A batch of one packet that isn't sample data (an IF Context packet). Its size differs from the data
// packets', so it can't share a GSO batch with them.
inline PacketBatch single_packet_batch(VrtPacket pkt) {
    PacketBatch batch;
    batch.used = pkt.bytes.size();
    batch.bytes = std::move(pkt.bytes);
    return batch;
}

// A stage that falls this far behind real time drops the late samples, like a capture device's FIFO overrun.
constexpr double kOverrunS = 100e-3;

// Running totals of a run, in samples, on a watch channel. main() creates it; the source and send_loop() each
// publish their own fields through their own WatchSender. A source that never drops needs no WatchSender.
// The three overrun counts say which stage of the source dropped the samples:
struct PaceStats {
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

// Turns on UDP GSO for `sock` with segments of `pkt_bytes` and returns how many packets of that size a source
// may put in one batch; 1, with a warning, if GSO is unavailable. See the PERFORMANCE NOTE in main.cpp's
// main().
size_t enable_gso(coro::UdpSocket& sock, size_t pkt_bytes);

// Sends every batch of `stream` to `sock` (already connected) until the stream ends, dropping each batch once
// sent. Publishes `sent` and `send_wait` on `paceTx` after every batch, runs paceMonitorLoop() meanwhile, and
// logs the run's totals at the end (`rate_hz` is the stream's nominal sample rate).
//
// A failed send throws std::system_error (e.g. ECONNREFUSED, surfaced from an earlier ICMP port-unreachable
// when no listener is bound), which ends the loop; so does anything the stream throws.
coro::Coro<void> send_loop(coro::UdpSocket sock, coro::CoroStream<PacketBatch> stream, double rate_hz,
                           coro::WatchSender<PaceStats> paceTx, coro::WatchReceiver<PaceStats> paceRx);

// Reports the samples a run dropped because a stage fell behind real time. A receiver sees these as timestamp
// gaps but can't tell whether they were lost here or in the network, so the sender says so itself. Runs as its
// own task, so it reports while the send loop is stuck (the source goes on publishing what it drops); reports
// once a second while dropping, and returns once every publisher has gone.
coro::Coro<void> paceMonitorLoop(coro::WatchReceiver<PaceStats> paceRx);

}  // namespace vita49_send
