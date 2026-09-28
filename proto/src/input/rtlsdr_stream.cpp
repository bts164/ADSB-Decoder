#include "input/rtlsdr_stream.h"

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <rtl-sdr.h>

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/io/signal.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/mpsc.h>
#include <coro/task/spawn_blocking.h>

#include "dsp/iq_block.h"

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

    IqBlock block = IqBlock::zeroed(ctx->pad, n, ctx->next_start);
    ctx->next_start += n;
    std::complex<float>* out = block.data();
    for (size_t i = 0; i < n; i++) {
        // librtlsdr delivers unsigned 8-bit offset-binary samples (128 ==
        // zero) -- recenter to signed. AdsbDemod::exec's per-block RMS
        // normalization (see demod.cpp) makes the exact input scale
        // otherwise unimportant.
        float i_val = (static_cast<float>(buf[2 * i]) - 127.5f) / 127.5f;
        float q_val = (static_cast<float>(buf[2 * i + 1]) - 127.5f) / 127.5f;
        out[i] = std::complex<float>(i_val, q_val);
    }

    if (!ctx->tx.try_send(std::move(block))) {
        // Buffer full -- the compute thread can't keep up with this block.
        // Drop it rather than blocking_send(): blocking here would stall
        // librtlsdr's USB callback thread and desync its internal transfer
        // resubmission, losing far more data than one dropped block does.
        if (++ctx->dropped % 100 == 1) {
            std::cerr << "[rtlsdr] warning: compute thread falling behind, " << ctx->dropped
                      << " block(s) dropped so far\n";
        }
    }
}

// RAII wrapper around a librtlsdr device handle -- see doc/guidelines.md
// CA.1 (RAII for all resources owned by a coroutine frame): the owning
// coroutine suspends across co_await points, so the device handle must be
// released by a destructor, not by an explicit close() call that a
// cancellation path could skip.
//
// Owns its own SIGINT shutdown: spawns a lightweight coroutine (via
// coro::spawn(), onto the ambient runtime) that awaits coro::signal(SIGINT)
// and calls close() when it fires. This means closing on interrupt needs no
// select()/orchestration at any call site -- whoever holds an RtlSdr just
// gets interrupted out from under them. If the device is closed for any
// other reason first (read error, normal stream end), ~JoinHandle cancels
// the still-pending watcher automatically (see join_handle.h's
// cancel-on-destroy default). A SIGINT raised before any RtlSdr exists is
// not handled -- an accepted gap, not a case this program needs to cover.
class RtlSdr {
public:
    explicit RtlSdr(int device_index) {
        if (rtlsdr_open(&m_dev, static_cast<uint32_t>(device_index)) != 0) {
            m_dev = nullptr;
            std::runtime_error e(std::format("failed to open rtl-sdr device {}", device_index));
            std::cerr << e.what() << "\n";
            throw e;
        }
        m_sigint_watcher = coro::spawn([this]() -> coro::Coro<void> {
            co_await coro::signal(SIGINT);
            std::cerr << "stopping after SIGINT\n";
            close();
        }());
    }

    ~RtlSdr() { close(); }

    RtlSdr(const RtlSdr&) = delete;
    RtlSdr& operator=(const RtlSdr&) = delete;

    void close() {
        if (nullptr != m_dev) {
            rtlsdr_close(m_dev);
            m_dev = nullptr;
        }
    }

    inline rtlsdr_dev_t* dev() const { return m_dev; }

private:
    rtlsdr_dev_t* m_dev = nullptr;
    coro::JoinHandle<void> m_sigint_watcher;
};

}  // namespace

// Streams raw IQ blocks off `device_index`: librtlsdr's own event-handling
// thread (inside rtlsdr_read_async, which blocks until the device is closed
// -- see RtlSdr above -- or a read error) delivers raw uint8 I/Q buffers via
// callback; the callback converts each buffer to a padded complex<float>
// block (same guard-padding contract exec() requires, see demod.h) and
// pushes it into an mpsc channel this generator drains.
//
// cb_ctx (and the sender it owns) is constructed *inside* the read-thread
// lambda, not in this function's frame: that makes it destruct exactly when
// rtlsdr_read_async returns, for any reason, which is exactly when the
// channel should close and let this generator's loop (and any consumer
// draining it) see exhaustion instead of hanging forever.
coro::CoroStream<IqBlock> rtlsdr_iq_stream(int device_index, double rate_hz, double freq_hz, double gain_db,
                                            IqPadding pad, uint32_t buf_num, uint32_t buf_len) {
    RtlSdr radio(device_index);
    rtlsdr_set_sample_rate(radio.dev(), static_cast<uint32_t>(rate_hz));
    rtlsdr_set_center_freq(radio.dev(), static_cast<uint32_t>(freq_hz));
    if (gain_db < 0.0) {
        rtlsdr_set_tuner_gain_mode(radio.dev(), 0);  // auto
    } else {
        rtlsdr_set_tuner_gain_mode(radio.dev(), 1);  // manual
        rtlsdr_set_tuner_gain(radio.dev(), static_cast<int>(std::lround(gain_db * 10.0)));
    }
    rtlsdr_reset_buffer(radio.dev());

    std::cerr << "streaming from rtl-sdr device " << device_index << ": rate=" << rate_hz << " Hz, freq=" << freq_hz
              << " Hz, gain=" << (gain_db < 0.0 ? std::string("auto") : std::to_string(gain_db) + " dB")
              << " (Ctrl+C to stop)\n";

    auto channel = coro::mpsc_channel<IqBlock>(4);
    coro::MpscSender<IqBlock> tx = std::move(channel.first);
    coro::MpscReceiver<IqBlock> rx = std::move(channel.second);

    auto read_handle = coro::spawn_blocking(
        [dev = radio.dev(), tx = std::move(tx), pad, buf_num, buf_len]() mutable {
            StreamCallbackCtx cb_ctx{std::move(tx), pad};
            return rtlsdr_read_async(dev, rtlsdr_stream_cb, &cb_ctx, buf_num, buf_len);
            // cb_ctx -- and the sender it owns -- destructs here, closing the channel.
        });

    while (auto block = co_await coro::next(rx)) {
        co_yield std::move(*block);
    }

    int rc = co_await read_handle;
    std::cerr << "rtl-sdr read thread ended (rc=" << rc << ")\n";
}
