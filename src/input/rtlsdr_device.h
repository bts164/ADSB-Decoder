#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>

#include <rtl-sdr.h>

#include <coro/coro.h>
#include <coro/task/join_handle.h>

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
 * RAII wrapper around a librtlsdr device handle -- see doc/guidelines.md CA.1 (RAII for all resources owned
 * by a coroutine frame): the owning coroutine suspends across co_await points, so the device handle must be
 * released by a destructor, not by an explicit close() call that a cancellation path could skip.
 *
 * read_async() streams from the device and survives it being unplugged: when the read ends without a stop
 * having been asked for, it closes the handle and reopens the device, at once and then after 1, 2, 4 and
 * every 8 seconds, until it is back.
 *
 * Owns its own SIGINT shutdown: spawns a lightweight coroutine (via coro::spawn(), onto the ambient runtime)
 * that awaits coro::signal(SIGINT) and asks read_async() to stop. That makes it return (closing the caller's
 * channel, so its stream ends) whether it is reading or waiting to reconnect, without closing the device on
 * a runtime thread, so interrupting needs no select()/orchestration at any call site -- whoever holds an
 * RtlSdr just gets interrupted out from under them, and the destructor closes the device. If the RtlSdr is
 * destroyed for any other reason first, ~JoinHandle cancels the still-pending watcher automatically (see
 * join_handle.h's cancel-on-destroy default).
 *
 * Accepted gap, not a case these programs need to cover: a SIGINT raised before any RtlSdr exists is not
 * handled.
 *
 * Not movable: the watcher and a running read_async() hold `this`.
 */
class RtlSdr {
public:
    /** Called on the read thread after a reconnect, with how long the device was gone. */
    using OnReconnect = std::function<void(std::chrono::duration<double> outage)>;

    /**
     * Opens the device, sets rate, frequency and gain, resets its buffer and logs the settings.
     * Must be called on a coro runtime thread (for the SIGINT watcher).
     * @throws std::runtime_error if the device can't be opened
     */
    explicit RtlSdr(const RtlSdrTuning& tuning);

    ~RtlSdr() { close(); }

    RtlSdr(const RtlSdr&) = delete;
    RtlSdr& operator=(const RtlSdr&) = delete;

    /**
     * Streams from the device until Ctrl+C or close(), calling `cb` on this thread with each buffer as
     * rtlsdr_read_async() does. Blocks, so call it on a coro::spawn_blocking thread. If the device goes away
     * it reconnects (see the class comment) and carries on with the same `cb`; no samples arrive meanwhile.
     * @param cb librtlsdr buffer callback
     * @param ctx passed to `cb`
     * @param buf_num librtlsdr async buffer count (0 = librtlsdr's default)
     * @param buf_len librtlsdr async buffer length in bytes (0 = librtlsdr's default)
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

    // A static member taking the object as a parameter, not a capturing lambda: a lambda coroutine's captures
    // live in the closure object, which is gone once the spawn() expression ends.
    static coro::Coro<void> stop_on_sigint(RtlSdr* self);
    static void relay_cb(unsigned char* buf, uint32_t len, void* relay);

    // Sets the stop flag and ends a running read or reconnect wait. Doesn't block.
    void request_stop();
    // Waits out the backoff schedule, reopening the device, until it is back (true) or a stop is asked for.
    // Takes and returns with `lock` held.
    bool reconnect(std::unique_lock<std::mutex>& lock);

    const RtlSdrTuning m_tuning;
    std::mutex m_mutex;                 // guards m_dev and m_reading
    std::condition_variable m_cv;       // signals a stop request and the end of read_async()
    std::atomic<bool> m_stop = false;
    rtlsdr_dev_t* m_dev = nullptr;      // nullptr while disconnected and once closed
    bool m_reading = false;             // read_async() is running
    coro::JoinHandle<void> m_sigint_watcher;
};
