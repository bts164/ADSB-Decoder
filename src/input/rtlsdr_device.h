#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>

#include <rtl-sdr.h>

/**
 * @file
 * An open, tuned RTL-SDR, shared by `adsb rtlsdr` (rtlsdr_stream.h) and vita49_send's rtlsdr source. What
 * each does with the samples (the rtlsdr_read_async callback) stays with its caller.
 */

/** Device selection and tuning. */
struct RtlSdrTuning {
    int device_index;
    double rate_hz;
    double freq_hz;
    double gain_db;  ///< tuner gain in dB, or negative for auto gain
};

/**
 * RAII wrapper around a librtlsdr device handle -- see coro's doc/guidelines.md CA.1 (RAII for all resources owned
 * by a coroutine frame): the owning coroutine suspends across co_await points, so the device handle must be
 * released by a destructor, not by an explicit close() call that a cancellation path could skip.
 *
 * read_async() streams from the device and survives it being unplugged: when the read ends without a stop
 * having been asked for, it closes the handle and reopens the device, at once and then after 1, 2, 4 and
 * every 8 seconds, until it is back. A device that opens but rejects the rate or frequency counts as not back.
 *
 * Knows nothing about Ctrl+C. read_async() runs until the RtlSdr is closed or destroyed: the program's
 * top-level SIGINT handler drops whatever owns the stream, the stream's coroutine frame is destroyed, and
 * ~RtlSdr stops the read. Cancelling the spawn_blocking task that runs read_async() is not enough by itself,
 * because the thread is inside librtlsdr, which is not a cancellation point; only close() (through
 * rtlsdr_cancel_async()) gets it out.
 *
 * Not movable: a running read_async() holds `this`.
 */
class RtlSdr {
public:
    /** Called on the read thread after a reconnect, with how long the device was gone. */
    using OnReconnect = std::function<void(std::chrono::duration<double> outage)>;

    /**
     * Opens the device, sets rate, frequency and gain, resets its buffer and logs the settings in effect. A
     * gain the device rejects, or one the tuner has no step for, is logged as a warning, and so is a rate the
     * device rounds by up to 100 ppm.
     * @throws std::runtime_error if the device can't be opened, rejects the rate or frequency, or rounds the rate
     *         by more than 100 ppm
     */
    explicit RtlSdr(const RtlSdrTuning& tuning);

    ~RtlSdr() { close(); }

    RtlSdr(const RtlSdr&) = delete;
    RtlSdr& operator=(const RtlSdr&) = delete;

    /**
     * Streams from the device until close(), calling `cb` on this thread with each buffer as
     * rtlsdr_read_async() does. Blocks, so call it on a coro::spawn_blocking thread. If the device goes away
     * it reconnects (see the class comment) and carries on with the same `cb`; no samples arrive meanwhile.
     * Logs the buffers it reads with.
     * @param cb librtlsdr buffer callback
     * @param ctx passed to `cb`
     * @param buf_num librtlsdr async buffer count (0 = librtlsdr's default)
     * @param buf_len librtlsdr async buffer length in bytes (0, or not a multiple of 512 = librtlsdr's default)
     * @param on_reconnect called before reading resumes, so the caller can account for the missing samples
     * @return the last rtlsdr_read_async() result
     */
    int read_async(rtlsdr_read_async_cb_t cb, void* ctx, uint32_t buf_num, uint32_t buf_len,
                   const OnReconnect& on_reconnect = {});

    /**
     * Stops read_async(), waits for it to return, then closes the device (idempotent). May block briefly.
     */
    void close();

private:
    struct Relay;

    static void relay_cb(unsigned char* buf, uint32_t len, void* relay);

    // Sets the stop flag and ends a running read or reconnect wait. Doesn't block.
    void request_stop();
    // Waits out the backoff schedule, reopening the device, until it is back (true) or a stop is asked for.
    // Takes and returns with `lock` held.
    bool reconnect(std::unique_lock<std::mutex>& lock);

    const RtlSdrTuning m_tuning;
    double m_rate_hz = 0;  // the rate the device runs at, within 100 ppm of the one asked for
    std::mutex m_mutex;                 // guards m_dev and m_reading
    std::condition_variable m_cv;       // signals a stop request and the end of read_async()
    std::atomic<bool> m_stop = false;
    rtlsdr_dev_t* m_dev = nullptr;      // nullptr while disconnected and once closed
    bool m_reading = false;             // read_async() is running
};
