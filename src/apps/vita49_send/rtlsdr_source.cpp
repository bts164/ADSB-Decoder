#include "apps/vita49_send/rtlsdr_source.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include "apps/vita49_send/vrt_encode.h"
#include "common/log.h"
#include "input/rtlsdr_device.h"

#include <coro/coro.h>
#include <coro/sync/mpsc.h>
#include <coro/task/spawn_blocking.h>

namespace vita49_send {
namespace {

struct StreamCallbackCtx {
    coro::MpscSender<VrtPacket> tx;
    size_t samples_per_packet;
    uint32_t stream_id;
    PacketClock clock;
    ContextSender context;
    uint32_t packet_count = 0;  // 4-bit VRT counter; advances per ENCODED packet, dropped or not
};

void rtlsdr_vrt_cb(unsigned char* buf, uint32_t len, void* ctx_ptr) {
    auto* ctx = static_cast<StreamCallbackCtx*>(ctx_ptr);
    const size_t n = len / 2;  // interleaved uint8 I/Q pairs
    if (n == 0) return;

    for (size_t offset = 0; offset < n; offset += ctx->samples_per_packet) {
        const size_t count = std::min(ctx->samples_per_packet, n - offset);
        // Best effort: it is repeated, so a drop here costs nothing lasting.
        if (auto context = ctx->context.next_if_due(ctx->clock)) ctx->tx.try_send(std::move(*context));
        const PacketClock::Stamp ts = ctx->clock.next(count);

        VrtPacket pkt = build_vrt_packet_u8(ctx->stream_id, ctx->packet_count, ts, buf + 2 * offset, count);
        // Counted even if the packet is dropped below, so a receiver sees the
        // gap in the 4-bit counter.
        ctx->packet_count = (ctx->packet_count + 1) & 0xF;
        if (!ctx->tx.try_send(std::move(pkt))) {
            // Buffer full -- the UDP send side can't keep up. Drop rather
            // than blocking_send(): see rtlsdr_stream.cpp's identical
            // reasoning (blocking here would desync librtlsdr's USB
            // transfer resubmission and lose far more data).
            static uint64_t dropped = 0;
            dropped++;
            LOGF_EVERY_N_SEC(WARNING, 1, "UDP send falling behind, %u packet(s) dropped so far", dropped);
        }
    }
}

coro::Coro<void> vrt_send_loop(coro::MpscReceiver<VrtPacket> rx, coro::UdpSocket sock) {
    while (auto pkt = co_await coro::next(rx)) {
        // send() throws std::system_error (e.g. ECONNREFUSED, surfaced from an
        // earlier ICMP port-unreachable when no listener is bound) and that
        // aborts the loop. Wrap in try/catch here if best-effort streaming
        // with no listener is ever wanted. See the PERFORMANCE NOTE in main.cpp's main().
        co_await sock.send(std::move(*pkt));
    }
}

}  // namespace

coro::Coro<void> rtlsdr_send_loop(coro::UdpSocket sock, RtlSdrSourceParams params) {
    RtlSdr radio({params.device_index, params.rate_hz, params.freq_hz, params.gain_db});

    // 1024 slots absorb the ~365-packet burst of one rtlsdr callback; see the PERFORMANCE NOTE's channel depth.
    auto channel = coro::mpsc_channel<VrtPacket>(1024);
    coro::MpscSender<VrtPacket> tx = std::move(channel.first);
    coro::MpscReceiver<VrtPacket> rx = std::move(channel.second);

    auto read_handle = coro::spawn_blocking([&radio, tx = std::move(tx), params]() mutable {
        const auto sample_rate_hz = static_cast<uint32_t>(params.rate_hz);
        StreamCallbackCtx cb_ctx{
            std::move(tx), params.samples_per_packet, params.stream_id, PacketClock{sample_rate_hz},
            ContextSender(params.stream_id, StreamConfig{params.rate_hz, params.freq_hz, params.rate_hz},
                          params.context_interval_s)};
        // The samples missed while the device was gone still count, so the timestamps after a reconnect
        // show the receiver the gap.
        auto skip_outage = [&](std::chrono::duration<double> outage) {
            cb_ctx.clock.samples_emitted += static_cast<uint64_t>(std::llround(outage.count() * params.rate_hz));
        };
        return radio.read_async(rtlsdr_vrt_cb, &cb_ctx, params.buf_num, params.buf_len, skip_outage);
        // cb_ctx -- and the sender it owns -- destructs here, closing the channel.
    });

    co_await vrt_send_loop(std::move(rx), std::move(sock));

    int rc = co_await read_handle;
    LOGF(INFO, "rtl-sdr read thread ended (rc=%d)", rc);
}

}  // namespace vita49_send
