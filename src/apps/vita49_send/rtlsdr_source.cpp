#include "apps/vita49_send/rtlsdr_source.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include <xtensor/xtensor.hpp>

#include "apps/vita49_send/packet_sender.h"
#include "apps/vita49_send/vrt_encode.h"
#include "common/log.h"
#include "input/rtlsdr_device.h"

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/watch.h>
#include <coro/task/spawn_blocking.h>

namespace vita49_send {
namespace {

// 1024 slots absorb the ~365-packet burst of one rtlsdr callback; see the PERFORMANCE NOTE's channel depth.
constexpr size_t kQueueItems = 1024;

struct StreamCallbackCtx {
    coro::MpscSender<PacketBatch> tx;
    coro::MpscReceiver<BatchBytes> free_rx;  // buffers that have been sent
    BatchPool pool;                          // where the batches send them back to
    coro::WatchSender<PaceStats> paceTx;
    size_t samples_per_packet;
    uint32_t stream_id;
    PacketClock clock;
    ContextSender context;
    uint32_t packet_count = 0;  // 4-bit VRT counter; advances per ENCODED packet, dropped or not
};

// Encodes one USB transfer into batches of one packet each and queues them. One packet per batch, not a GSO
// batch: at 2.4 Msps the per-datagram cost doesn't matter (see the PERFORMANCE NOTE's batching), and plain sends
// work on every interface.
void rtlsdr_vrt_cb(unsigned char* buf, uint32_t len, void* ctx_ptr) {
    auto* ctx = static_cast<StreamCallbackCtx*>(ctx_ptr);
    const size_t n = len / 2;  // interleaved uint8 I/Q pairs
    if (n == 0) return;

    const size_t pkt_capacity = vrt_packet_bytes(ctx->samples_per_packet);
    uint64_t dropped = 0;  // samples
    for (size_t offset = 0; offset < n; offset += ctx->samples_per_packet) {
        const size_t count = std::min(ctx->samples_per_packet, n - offset);
        // Best effort: it is repeated, so a drop here costs nothing lasting.
        if (auto context = ctx->context.next_if_due(ctx->clock)) {
            (void)ctx->tx.try_send(single_packet_batch(std::move(*context)));
        }
        const PacketClock::Stamp ts = ctx->clock.next(count);

        PacketBatch batch;
        // Reuses a buffer that has been sent, or allocates one while the pool is still filling up.
        if (auto free_buf = ctx->free_rx.try_recv()) {
            batch.bytes = std::move(*free_buf);
        } else {
            batch.bytes = BatchBytes::from_shape({pkt_capacity});
        }
        batch.pool = ctx->pool;
        write_vrt_packet_u8(batch.bytes.data(), ctx->stream_id, ctx->packet_count, ts, buf + 2 * offset, count);
        batch.used = vrt_packet_bytes(count);
        batch.samples = count;
        // Counted even if the packet is dropped below, so a receiver sees the
        // gap in the 4-bit counter.
        ctx->packet_count = (ctx->packet_count + 1) & 0xF;
        if (!ctx->tx.try_send(std::move(batch))) {
            // Queue full -- the UDP send side can't keep up. Drop rather
            // than blocking_send(): see rtlsdr_stream.cpp's identical
            // reasoning (blocking here would desync librtlsdr's USB
            // transfer resubmission and lose far more data). The dropped
            // batch's buffer goes straight back to the pool.
            dropped += count;
        }
    }
    if (dropped > 0) {
        // The pace monitor reports these, at most once a second.
        ctx->paceTx.send_if_modified([&](PaceStats& stats) {
            stats.overrun_queue_full += dropped;
            return true;
        });
    }
}

}  // namespace

// cb_ctx (and the sender it owns) is constructed inside the read-thread lambda, not in this function's frame:
// that makes it destruct exactly when the read returns, for any reason, which is when the channel should
// close and end the stream.
coro::CoroStream<PacketBatch> rtlsdr_packet_stream(RtlSdrSourceParams params, coro::WatchSender<PaceStats> paceTx) {
    RtlSdr radio({params.device_index, params.rate_hz, params.freq_hz, params.gain_db});

    auto [tx, rx] = coro::mpsc_channel<PacketBatch>(kQueueItems);
    // Buffers come back for reuse (see PacketBatch); a couple more than the queue covers the batch each stage
    // holds.
    auto [free_tx, free_rx] = coro::mpsc_channel<BatchBytes>(kQueueItems + 2);
    BatchPool pool = std::make_shared<coro::MpscSender<BatchBytes>>(std::move(free_tx));

    auto read_handle = coro::spawn_blocking([&radio, tx = std::move(tx), free_rx = std::move(free_rx),
                                             pool = std::move(pool), paceTx = std::move(paceTx), params]() mutable {
        const auto sample_rate_hz = static_cast<uint32_t>(params.rate_hz);
        StreamCallbackCtx cb_ctx{
            .tx = std::move(tx),
            .free_rx = std::move(free_rx),
            .pool = std::move(pool),
            .paceTx = std::move(paceTx),
            .samples_per_packet = params.samples_per_packet,
            .stream_id = params.stream_id,
            .clock = PacketClock{sample_rate_hz},
            .context = ContextSender(params.stream_id, StreamConfig{params.rate_hz, params.freq_hz, params.rate_hz},
                                     params.context_interval_s)};
        // The samples missed while the device was gone still count, so the timestamps after a reconnect
        // show the receiver the gap.
        auto skip_outage = [&](std::chrono::duration<double> outage) {
            cb_ctx.clock.samples_emitted += static_cast<uint64_t>(std::llround(outage.count() * params.rate_hz));
        };
        return radio.read_async(rtlsdr_vrt_cb, &cb_ctx, params.buf_num, params.buf_len, skip_outage);
        // cb_ctx -- and the sender it owns -- destructs here, closing the channel.
    });

    while (auto batch = co_await coro::next(rx)) {
        co_yield std::move(*batch);
    }

    int rc = co_await read_handle;
    LOGF(INFO, "rtl-sdr read thread ended (rc=%d)", rc);
}

}  // namespace vita49_send
