#pragma once

#include <cstdint>
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

/** vita49_iq_stream() settings. */
struct Vita49Options {
    std::string bind_host = "0.0.0.0";
    uint16_t port = 0;
    double sample_rate_hz = 0;          /**< the sender's rate; an IF Context packet that disagrees ends the stream */
    std::optional<uint32_t> stream_id;  /**< if unset, the first stream seen */
    double idle_timeout_s = 2.0;        /**< end the stream after this long with no packets (0 = never) */
};

/**
 * Binds a UDP socket and streams IqBlocks until Ctrl+C or the sender goes quiet for
 * Vita49Options::idle_timeout_s.
 *
 * A receive task on the runtime keeps draining the socket while the demod thread is busy, handing blocks
 * over through a small channel. If the demod falls behind and the channel fills, blocks are dropped
 * rather than letting the kernel socket buffer overflow.
 * @param opts socket and stream settings
 * @param pad guard padding to allocate around each block (AdsbDemod::padding())
 * @param chunk_samples samples per block; the last block may be shorter
 * @throws std::runtime_error (when the stream is consumed) if the bind fails or the stream's IF Context
 *         packets advertise a different sample rate
 */
coro::CoroStream<IqBlock> vita49_iq_stream(Vita49Options opts, IqPadding pad, uint32_t chunk_samples);

/** Default block size, the same as kFileStreamChunkSamples. */
constexpr uint32_t kVita49StreamChunkSamples = 128 * 1024;
