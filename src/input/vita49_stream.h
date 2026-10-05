#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <coro/coro.h>
#include <coro/coro_stream.h>

#include "dsp/iq_block.h"

/**
 * @file
 * `adsb vita49` source: receives a VITA-49 (VRT) IQ stream over UDP.
 *
 * Receives the stream vita49_send emits (doc/design/vita49-format.md): IF Data packets carrying
 * big-endian int16 IQ, plus periodic IF Context packets carrying the sample rate and RF frequency.
 */

/**
 * How much of the real-time signal a live input got through to the demodulator over a stretch of wall-clock
 * time, and where the rest went. vita49_iq_stream() reports one every couple of seconds while packets arrive.
 */
struct InputStatus {
    double window_s = 0;                   /**< wall-clock time covered */
    double rate_hz = 0;                    /**< the expected sample rate */
    double stream_s = 0;                   /**< signal time the stream advanced by its timestamps (includes lost_s) */
    double lost_s = 0;                     /**< of stream_s, gaps where packets never arrived (zero-filled) */
    double dropped_s = 0;                  /**< of stream_s, samples dropped because the demodulator was behind */
    /** Times the kernel dropped for a full socket receive buffer (Linux): each time one datagram, or with GRO a
     * whole coalesced batch (up to ~44). */
    std::optional<uint64_t> kernel_drops;

    /** Signal time per wall-clock time: below 1, the source sends slower than real time. */
    double realtime() const { return window_s > 0 ? stream_s / window_s : 0; }
    /** Fraction of the real-time signal that was demodulated. Slightly low when a dropped block held a gap
     *  (counted in both lost_s and dropped_s). */
    double delivered() const {
        const double d = window_s > 0 ? (stream_s - lost_s - dropped_s) / window_s : 0;
        return d > 0 ? d : 0;
    }
    /** Whether the whole real-time signal got through (allowing for timing jitter). */
    bool ok() const;
    /** One line: how much got through and, if not all, why. */
    std::string describe() const;
};

/** vita49_iq_stream() settings. */
struct Vita49Options {
    std::string bind_host = "0.0.0.0";
    uint16_t port = 0;
    double sample_rate_hz = 0;          /**< the sender's rate; an IF Context packet that disagrees ends the stream */
    std::optional<uint32_t> stream_id;  /**< if unset, the first stream seen */
    double idle_timeout_s = 2.0;        /**< end the stream after this long with no packets (0 = never) */
    /** Called with each InputStatus, on the runtime thread; they're also printed to stderr when not ok(). */
    std::function<void(const InputStatus&)> on_status;
};

/**
 * Binds a UDP socket and streams IqBlocks until Ctrl+C or the sender goes quiet for
 * Vita49Options::idle_timeout_s.
 *
 * A receive task on the runtime keeps draining the socket while the demod thread is busy, handing blocks
 * over through a small channel. If the demod falls behind and the channel fills, blocks are dropped
 * rather than letting the kernel socket buffer overflow.
 *
 * Every couple of seconds it works out an InputStatus: whether the stream kept up with real time and how
 * much was lost in transit or dropped. One that isn't ok() is printed to stderr as a warning, since the
 * decoded output alone gives no sign of it (a slow sender's timestamps still form a gap-free stream).
 * @param opts socket and stream settings
 * @param pad guard padding to allocate around each block (AdsbDemod::padding())
 * @param chunk_samples samples per block; the last block may be shorter
 * @throws std::runtime_error (when the stream is consumed) if the bind fails or the stream's IF Context
 *         packets advertise a different sample rate
 */
coro::CoroStream<IqBlock> vita49_iq_stream(Vita49Options opts, IqPadding pad, uint32_t chunk_samples);

/** Default block size, the same as kFileStreamChunkSamples. */
constexpr uint32_t kVita49StreamChunkSamples = 128 * 1024;
