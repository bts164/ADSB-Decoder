#include "input/rtlsdr_device.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <csignal>
#include <stdexcept>
#include <string>

#include <coro/coro.h>
#include <coro/io/signal.h>
#include <coro/runtime/runtime.h>

#include "common/log.h"

namespace {

// Wait before each reconnect attempt: at once, then doubling up to the last entry, which repeats.
constexpr std::array<std::chrono::seconds, 5> kReconnectBackoff{
    std::chrono::seconds{0}, std::chrono::seconds{1}, std::chrono::seconds{2}, std::chrono::seconds{4},
    std::chrono::seconds{8}};

// Opens and tunes the device; nullptr if it can't be opened.
rtlsdr_dev_t* open_tuned(const RtlSdrTuning& tuning) {
    rtlsdr_dev_t* dev = nullptr;
    if (rtlsdr_open(&dev, static_cast<uint32_t>(tuning.device_index)) != 0) return nullptr;

    rtlsdr_set_sample_rate(dev, static_cast<uint32_t>(tuning.rate_hz));
    rtlsdr_set_center_freq(dev, static_cast<uint32_t>(tuning.freq_hz));
    if (tuning.gain_db < 0.0) {
        rtlsdr_set_tuner_gain_mode(dev, 0);  // auto
    } else {
        rtlsdr_set_tuner_gain_mode(dev, 1);  // manual
        rtlsdr_set_tuner_gain(dev, static_cast<int>(std::lround(tuning.gain_db * 10.0)));
    }
    rtlsdr_reset_buffer(dev);
    return dev;
}

}  // namespace

// What librtlsdr calls back with: the caller's callback, behind a check of the stop flag.
struct RtlSdr::Relay {
    RtlSdr* self;
    rtlsdr_dev_t* dev;
    rtlsdr_read_async_cb_t cb;
    void* ctx;
};

RtlSdr::RtlSdr(const RtlSdrTuning& tuning) : m_tuning(tuning) {
    m_dev = open_tuned(tuning);
    if (nullptr == m_dev)
        throw std::runtime_error("failed to open rtl-sdr device " + std::to_string(tuning.device_index));
    m_sigint_watcher = coro::spawn(stop_on_sigint(this));

    LOGF(INFO, "streaming from rtl-sdr device %d: rate=%g Hz, freq=%g Hz, gain=%s (Ctrl+C to stop)",
         tuning.device_index, tuning.rate_hz, tuning.freq_hz,
         tuning.gain_db < 0.0 ? std::string("auto") : absl::StrFormat("%g dB", tuning.gain_db));
}

int RtlSdr::read_async(rtlsdr_read_async_cb_t cb, void* ctx, uint32_t buf_num, uint32_t buf_len,
                       const OnReconnect& on_reconnect) {
    std::unique_lock lock(m_mutex);
    m_reading = true;
    int rc = 0;
    while (!m_stop && nullptr != m_dev) {
        Relay relay{this, m_dev, cb, ctx};
        lock.unlock();
        rc = rtlsdr_read_async(relay.dev, relay_cb, &relay, buf_num, buf_len);
        lock.lock();
        // Only a stop request or a lost device ends the read: librtlsdr cancels it itself when the USB
        // transfers start failing.
        if (m_stop) break;

        const auto lost_at = std::chrono::steady_clock::now();
        LOGF(WARNING, "rtl-sdr device %d disconnected (read ended with rc=%d); reconnecting",
             m_tuning.device_index, rc);
        rtlsdr_close(m_dev);
        m_dev = nullptr;
        if (!reconnect(lock)) break;

        const std::chrono::duration<double> outage = std::chrono::steady_clock::now() - lost_at;
        LOGF(INFO, "rtl-sdr device %d reconnected after %.1f s", m_tuning.device_index, outage.count());
        if (on_reconnect) on_reconnect(outage);
    }
    m_reading = false;
    m_cv.notify_all();
    return rc;
}

bool RtlSdr::reconnect(std::unique_lock<std::mutex>& lock) {
    for (size_t attempt = 0;; attempt++) {
        const auto wait = kReconnectBackoff[std::min(attempt, kReconnectBackoff.size() - 1)];
        if (m_cv.wait_for(lock, wait, [&] { return m_stop.load(); })) return false;

        // Opening takes a while, so not under the lock: request_stop() must not block a runtime thread.
        lock.unlock();
        rtlsdr_dev_t* dev = open_tuned(m_tuning);
        lock.lock();
        if (nullptr != dev) {
            m_dev = dev;  // closed by close() if a stop was asked for meanwhile
            return !m_stop;
        }
        const auto next = kReconnectBackoff[std::min(attempt + 1, kReconnectBackoff.size() - 1)];
        LOGF(WARNING, "rtl-sdr device %d not found; trying again in %d s", m_tuning.device_index, next.count());
    }
}

void RtlSdr::relay_cb(unsigned char* buf, uint32_t len, void* relay_ptr) {
    auto* relay = static_cast<Relay*>(relay_ptr);
    // rtlsdr_cancel_async() only acts on a read that has started streaming, so a stop request that arrives
    // just before the read starts is caught here, on its first buffer.
    if (relay->self->m_stop.load(std::memory_order_relaxed)) {
        rtlsdr_cancel_async(relay->dev);
        return;
    }
    relay->cb(buf, len, relay->ctx);
}

void RtlSdr::request_stop() {
    m_stop = true;
    {
        std::lock_guard lock(m_mutex);
        // A no-op (returns -2) when no read is running.
        if (nullptr != m_dev) rtlsdr_cancel_async(m_dev);
    }
    m_cv.notify_all();
}

void RtlSdr::close() {
    request_stop();
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [&] { return !m_reading; });
    if (nullptr != m_dev) {
        rtlsdr_close(m_dev);
        m_dev = nullptr;
    }
}

coro::Coro<void> RtlSdr::stop_on_sigint(RtlSdr* self) {
    co_await coro::signal(SIGINT);
    LOGF(INFO, "stopping after SIGINT");
    // Only asks the read to stop: closing the device would block this runtime thread until the read
    // returns. The read's return ends the caller's stream, and ~RtlSdr closes the device.
    // Race (accepted): ~RtlSdr may be running on another thread if SIGINT lands during teardown -- the
    // watcher is only cancelled after the destructor body has run.
    self->request_stop();
}
