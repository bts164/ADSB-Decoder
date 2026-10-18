#include "apps/vita49_send/rtlsdr_source.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <utility>

#include <xtensor/xtensor.hpp>

#include "apps/vita49_send/pace_stats.h"
#include "apps/vita49_send/packet_batch.h"
#include "common/log.h"
#include "common/trace.h"
#include "input/rtlsdr_device.h"
#include "vrt/encode.h"

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/watch.h>
#include <coro/task/spawn_blocking.h>

namespace {

// 1024 slots absorb the ~365-packet burst of one rtlsdr callback.
constexpr size_t kQueueItems = 1024;

struct StreamCallbackCtx {
    coro::MpscSender<PacketBatch> tx;
    std::shared_ptr<net::BatchPool> pool;
    coro::WatchSender<PaceStats> pace_tx;
    size_t samples_per_packet;
    uint32_t stream_id;
    vrt::PacketClock clock;
    vrt::ContextSender context;
    uint32_t packet_count = 0;  // 4-bit VRT counter; advances per ENCODED packet, dropped or not
};

// Encodes one USB transfer into batches of one packet each and queues them. One packet per batch, not a GSO
// batch: at 2.4 Msps the per-datagram cost doesn't matter, and plain sends work on every interface.
void rtlsdr_vrt_cb(unsigned char* buf, uint32_t len, void* ctx_ptr) {
    auto* ctx = static_cast<StreamCallbackCtx*>(ctx_ptr);
    const size_t n = len / 2;  // interleaved uint8 I/Q pairs
    if (n == 0) return;
    TRACE_ZONE_VALUE("rtlsdr_encode", n);

    uint64_t dropped = 0;  // samples
    for (size_t offset = 0; offset < n; offset += ctx->samples_per_packet) {
        const size_t count = std::min(ctx->samples_per_packet, n - offset);
        // Best effort: it is repeated, so a drop here costs nothing lasting.
        if (auto context = ctx->context.next_if_due(ctx->clock)) {
            (void)ctx->tx.try_send(single_packet_batch(std::move(*context)));
        }
        const vrt::Timestamp ts = ctx->clock.next(count);

        PacketBatch batch{.datagrams = ctx->pool->acquire(), .samples = count};
        vrt::write_data_packet_u8(batch.datagrams.bytes.data(), ctx->stream_id, ctx->packet_count, ts,
                                  buf + 2 * offset, count);
        batch.datagrams.used = vrt::data_packet_bytes(count);
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
        count_dropped(ctx->pace_tx, &PaceStats::overrun_queue_full, dropped);
    }
}

}  // namespace

void RtlSdrSourceParams::add_options(argparse::ArgumentParser& cmd) {
    cmd.add_description("Stream from a live RTL-SDR.");
    cmd.add_argument("--device").scan<'d', int>().default_value(0).help("RTL-SDR device index");
    cmd.add_argument("--gain").scan<'g', double>().default_value(-1.0).help(
        "tuner gain in dB, or negative for auto gain");
    cmd.add_argument("--buf-num").scan<'u', unsigned>().default_value(0u).help(
        "number of librtlsdr async transfer buffers in flight (0 = librtlsdr default: 15)");
    cmd.add_argument("--buf-len").scan<'u', unsigned>().default_value(0u).help(
        "librtlsdr async transfer buffer length in bytes, multiple of 512 "
        "(0 = librtlsdr default: 16*32*512 = 262144 bytes)");
}

RtlSdrSourceParams RtlSdrSourceParams::from_options(const argparse::ArgumentParser& program,
                                                    const argparse::ArgumentParser& cmd) {
    return {
        .device_index = cmd.get<int>("--device"),
        .rate_hz = program.get<double>("--rate"),
        .freq_hz = program.get<double>("--freq"),
        .gain_db = cmd.get<double>("--gain"),
        .buf_num = cmd.get<unsigned>("--buf-num"),
        .buf_len = cmd.get<unsigned>("--buf-len"),
        .samples_per_packet = program.get<unsigned>("--samples-per-packet"),
        .stream_id = program.get<unsigned>("--stream-id"),
        .context_interval_s = program.get<double>("--context-interval"),
    };
}

// cb_ctx (and the sender it owns) is constructed inside the read-thread lambda, not in this function's frame:
// that makes it destruct exactly when the read returns, for any reason, which is when the channel should
// close and end the stream.
coro::CoroStream<PacketBatch> rtlsdr_packet_stream(RtlSdrSourceParams params, coro::WatchSender<PaceStats> pace_tx) {
    RtlSdr radio({params.device_index, params.rate_hz, params.freq_hz, params.gain_db});

    auto [tx, rx] = coro::mpsc_channel<PacketBatch>(kQueueItems);
    auto pool = net::BatchPool::create(vrt::data_packet_bytes(params.samples_per_packet));

    auto read_handle = coro::spawn_blocking([&radio, tx = std::move(tx), pool = std::move(pool),
                                             pace_tx = std::move(pace_tx), params]() mutable {
        const auto sample_rate_hz = static_cast<uint32_t>(params.rate_hz);
        StreamCallbackCtx cb_ctx{
            .tx = std::move(tx),
            .pool = std::move(pool),
            .pace_tx = std::move(pace_tx),
            .samples_per_packet = params.samples_per_packet,
            .stream_id = params.stream_id,
            .clock = vrt::PacketClock{sample_rate_hz},
            .context = vrt::ContextSender(params.stream_id,
                                          vrt::StreamConfig{params.rate_hz, params.freq_hz, params.rate_hz},
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
