#pragma once

#include <cstdint>
#include <optional>

#include <coro/coro_stream.h>

#include "dsp/iq_block.h"

/**
 * @file
 * `adsb rtlsdr` source: streams from a live RTL-SDR.
 */

/**
 * Streams IqBlocks from an RTL-SDR until Ctrl+C. If the device is unplugged the stream pauses until it has
 * reconnected (RtlSdr::read_async), then resumes with block starts advanced by the samples missed.
 *
 * librtlsdr's own thread (inside rtlsdr_read_async) delivers raw uint8 I/Q buffers to a callback, which
 * converts each one to a guard-padded IqBlock and pushes it into a small channel this stream drains. One
 * block per librtlsdr buffer. If the demod falls behind and the channel fills, blocks are dropped rather
 * than stalling librtlsdr's USB thread.
 * @param device_index RTL-SDR device index
 * @param rate_hz sample rate
 * @param freq_hz tuner center frequency
 * @param gain_db tuner gain, or negative for auto gain
 * @param pad guard padding to allocate around each block (AdsbDemod::padding())
 * @param buf_num librtlsdr async buffer count (default: librtlsdr's own)
 * @param buf_len librtlsdr async buffer length in bytes, a multiple of 512 (default: librtlsdr's own)
 */
coro::CoroStream<IqBlock> rtlsdr_iq_stream(int device_index, double rate_hz, double freq_hz, double gain_db,
                                            IqPadding pad, std::optional<uint32_t> buf_num = std::nullopt,
                                            std::optional<uint32_t> buf_len = std::nullopt);
