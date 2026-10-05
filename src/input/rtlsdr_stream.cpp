#include "input/rtlsdr_stream.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <span>

#include <rtl-sdr.h>

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/sync/mpsc.h>
#include <coro/task/spawn_blocking.h>

#include "common/log.h"
#include "dsp/iq_block.h"
#include "input/rtlsdr_device.h"

namespace {

struct StreamCallbackCtx {
    coro::MpscSender<IqBlock> tx;
    IqPadding pad;
    uint64_t next_start = 0;  // stream index of the next buffer's first sample, counting dropped ones
    uint64_t dropped = 0;
};

void rtlsdr_stream_cb(unsigned char* buf, uint32_t len, void* ctx_ptr) {
    auto* ctx = static_cast<StreamCallbackCtx*>(ctx_ptr);
    const size_t n = len / 2;  // interleaved uint8 I/Q pairs
    if (n == 0) return;

    IqBlock block = IqBlock::uninitialized(ctx->pad, n, ctx->next_start);
    ctx->next_start += n;
    // librtlsdr delivers unsigned 8-bit offset-binary samples (128 ==
    // zero) -- recenter to signed. AdsbDemod::exec's per-block RMS
    // normalization (see demod.cpp) makes the exact input scale
    // otherwise unimportant. Both are interleaved I/Q, so it is one
    // output float per input byte.
    const std::span<const unsigned char> in(buf, 2 * n);
    const std::span<float> out = block.iq();
    for (size_t i = 0; i < out.size(); i++) out[i] = (static_cast<float>(in[i]) - 127.5f) / 127.5f;

    if (!ctx->tx.try_send(std::move(block))) {
        // Buffer full -- the compute thread can't keep up with this block.
        // Drop it rather than blocking_send(): blocking here would stall
        // librtlsdr's USB callback thread and desync its internal transfer
        // resubmission, losing far more data than one dropped block does.
        ctx->dropped++;
        LOGF_EVERY_N_SEC(WARNING, 1, "compute thread falling behind, %u block(s) dropped so far", ctx->dropped);
    }
}

}  // namespace

// Streams raw IQ blocks off `device_index`: librtlsdr's own event-handling
// thread (inside RtlSdr::read_async, which blocks until Ctrl+C, reconnecting
// if the device is unplugged -- see rtlsdr_device.h) delivers raw uint8 I/Q buffers via
// callback; the callback converts each buffer to a padded float I/Q
// block (same guard-padding contract exec() requires, see demod.h) and
// pushes it into an mpsc channel this generator drains.
//
// cb_ctx (and the sender it owns) is constructed *inside* the read-thread
// lambda, not in this function's frame: that makes it destruct exactly when
// the read returns, for any reason, which is exactly when the
// channel should close and let this generator's loop (and any consumer
// draining it) see exhaustion instead of hanging forever.
coro::CoroStream<IqBlock> rtlsdr_iq_stream(int device_index, double rate_hz, double freq_hz, double gain_db,
                                            IqPadding pad, std::optional<uint32_t> buf_num,
                                            std::optional<uint32_t> buf_len) {
    RtlSdr radio({device_index, rate_hz, freq_hz, gain_db});

    auto channel = coro::mpsc_channel<IqBlock>(4);
    coro::MpscSender<IqBlock> tx = std::move(channel.first);
    coro::MpscReceiver<IqBlock> rx = std::move(channel.second);

    auto read_handle = coro::spawn_blocking(
        [&radio, tx = std::move(tx), pad, buf_num, buf_len, rate_hz]() mutable {
            StreamCallbackCtx cb_ctx{std::move(tx), pad};
            // The samples missed while the device was gone still count, so stream time keeps pace with the
            // wall clock and the blocks either side of the outage aren't stitched together.
            auto skip_outage = [&](std::chrono::duration<double> outage) {
                cb_ctx.next_start += static_cast<uint64_t>(std::llround(outage.count() * rate_hz));
            };
            // librtlsdr takes 0 to mean its own default.
            return radio.read_async(rtlsdr_stream_cb, &cb_ctx, buf_num.value_or(0), buf_len.value_or(0),
                                    skip_outage);
            // cb_ctx -- and the sender it owns -- destructs here, closing the channel.
        });

    while (auto block = co_await coro::next(rx)) {
        co_yield std::move(*block);
    }

    int rc = co_await read_handle;
    LOGF(INFO, "rtl-sdr read thread ended (rc=%d)", rc);
}
