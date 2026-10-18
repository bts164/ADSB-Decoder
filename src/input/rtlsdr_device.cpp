#include "input/rtlsdr_device.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include <absl/strings/str_join.h>

#include "common/log.h"

namespace {

// Wait before each reconnect attempt: at once, then doubling up to the last entry, which repeats.
constexpr std::array<std::chrono::seconds, 5> kReconnectBackoff{
    std::chrono::seconds{0}, std::chrono::seconds{1}, std::chrono::seconds{2}, std::chrono::seconds{4},
    std::chrono::seconds{8}};

// librtlsdr's transfer buffers when read_async() is given 0, or a length that isn't a multiple of 512. Its
// header doesn't export them.
constexpr uint32_t kDefaultBufNum = 15;
constexpr uint32_t kDefaultBufLen = 16 * 32 * 512;

// Opens and tunes the device. nullptr, with the reason in `error`, if it can't be opened or rejects the rate or
// frequency: streaming at other settings would mislabel every sample. A rejected gain is only logged.
rtlsdr_dev_t* open_tuned(const RtlSdrTuning& tuning, std::string& error) {
    rtlsdr_dev_t* dev = nullptr;
    if (rtlsdr_open(&dev, static_cast<uint32_t>(tuning.device_index)) != 0) {
        error = absl::StrFormat("failed to open rtl-sdr device %d", tuning.device_index);
        return nullptr;
    }
    if (rtlsdr_set_sample_rate(dev, static_cast<uint32_t>(tuning.rate_hz)) != 0) {
        error = absl::StrFormat("rtl-sdr device %d rejected sample rate %g Hz", tuning.device_index, tuning.rate_hz);
    } else if (rtlsdr_set_center_freq(dev, static_cast<uint32_t>(tuning.freq_hz)) != 0) {
        error = absl::StrFormat("rtl-sdr device %d rejected center frequency %g Hz", tuning.device_index,
                                tuning.freq_hz);
    }
    if (!error.empty()) {
        rtlsdr_close(dev);
        return nullptr;
    }
    if (tuning.gain_db < 0.0) {
        if (rtlsdr_set_tuner_gain_mode(dev, 0) != 0)  // auto
            LOGF(WARNING, "rtl-sdr device %d rejected auto gain", tuning.device_index);
    } else if (rtlsdr_set_tuner_gain_mode(dev, 1) != 0 ||  // manual
               rtlsdr_set_tuner_gain(dev, static_cast<int>(std::lround(tuning.gain_db * 10.0))) != 0) {
        LOGF(WARNING, "rtl-sdr device %d rejected gain %g dB", tuning.device_index, tuning.gain_db);
    }
    rtlsdr_reset_buffer(dev);
    return dev;
}

// A manual gain the tuner has no step for is quietly replaced by a step near it (the R820T takes the first one
// at or above), and librtlsdr reports the gain as asked for. So say so, and list the steps.
void check_gain_step(rtlsdr_dev_t* dev, const RtlSdrTuning& tuning) {
    if (tuning.gain_db < 0.0) return;
    const int count = rtlsdr_get_tuner_gains(dev, nullptr);
    if (count <= 0) return;
    std::vector<int> steps(static_cast<size_t>(count));  // tenths of a dB
    rtlsdr_get_tuner_gains(dev, steps.data());
    if (std::ranges::find(steps, std::lround(tuning.gain_db * 10.0)) != steps.end()) return;
    LOGF(WARNING, "rtl-sdr device %d: %g dB is not one of the tuner's gain steps, so it uses one near it. Steps "
         "(dB): %s", tuning.device_index, tuning.gain_db,
         absl::StrJoin(steps, " ", [](std::string* out, int g) { absl::StrAppendFormat(out, "%.1f", g / 10.0); }));
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
    std::string error;
    m_dev = open_tuned(tuning, error);
    if (nullptr == m_dev) throw std::runtime_error(error);

    // The rate and frequency the device reports, which are what it uses: the resampler can round the rate.
    m_rate_hz = rtlsdr_get_sample_rate(m_dev);
    m_freq_hz = rtlsdr_get_center_freq(m_dev);
    LOGF(INFO, "streaming from rtl-sdr device %d: rate=%.0f Hz, freq=%.0f Hz, gain=%s (Ctrl+C to stop)",
         tuning.device_index, m_rate_hz, m_freq_hz,
         tuning.gain_db < 0.0 ? std::string("auto") : absl::StrFormat("%g dB", tuning.gain_db));
    if (std::abs(m_rate_hz - tuning.rate_hz) >= 1.0)
        LOGF(WARNING, "rtl-sdr device %d runs at %.0f Hz, not the %g Hz asked for", tuning.device_index, m_rate_hz,
             tuning.rate_hz);
    check_gain_step(m_dev, tuning);
}

int RtlSdr::read_async(rtlsdr_read_async_cb_t cb, void* ctx, uint32_t buf_num, uint32_t buf_len,
                       const OnReconnect& on_reconnect) {
    // Resolved here rather than by librtlsdr, which doesn't report what it chose.
    if (buf_len % 512 != 0) {
        LOGF(WARNING, "rtl-sdr buffer length %u is not a multiple of 512; using librtlsdr's default instead",
             buf_len);
        buf_len = 0;
    }
    const uint32_t num = buf_num ? buf_num : kDefaultBufNum;
    const uint32_t len = buf_len ? buf_len : kDefaultBufLen;
    LOGF(INFO, "rtl-sdr device %d: %u transfer buffers%s of %u bytes%s, %.1f ms each", m_tuning.device_index, num,
         buf_num ? "" : " (librtlsdr's default)", len, buf_len ? "" : " (librtlsdr's default)",
         len / 2 / m_rate_hz * 1e3);

    std::unique_lock lock(m_mutex);
    m_reading = true;
    int rc = 0;
    while (!m_stop && nullptr != m_dev) {
        Relay relay{this, m_dev, cb, ctx};
        lock.unlock();
        rc = rtlsdr_read_async(relay.dev, relay_cb, &relay, num, len);
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
        std::string error;
        rtlsdr_dev_t* dev = open_tuned(m_tuning, error);
        lock.lock();
        if (nullptr != dev) {
            m_dev = dev;  // closed by close() if a stop was asked for meanwhile
            return !m_stop;
        }
        const auto next = kReconnectBackoff[std::min(attempt + 1, kReconnectBackoff.size() - 1)];
        LOGF(WARNING, "%s; trying again in %d s", error, next.count());
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
