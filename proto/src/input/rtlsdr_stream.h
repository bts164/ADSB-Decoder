#pragma once

#include <cstdint>

#include <coro/coro_stream.h>

#include "dsp/iq_block.h"

/**
 * @file
 * `adsb rtlsdr` source: streams from a live RTL-SDR.
 */

/**
 * Streams IqBlocks from an RTL-SDR until Ctrl+C or a read error.
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
 * @param buf_num librtlsdr async buffer count (0 = librtlsdr default)
 * @param buf_len librtlsdr async buffer length in bytes, a multiple of 512 (0 = librtlsdr default)
 */
coro::CoroStream<IqBlock> rtlsdr_iq_stream(int device_index, double rate_hz, double freq_hz, double gain_db,
                                            IqPadding pad, uint32_t buf_num, uint32_t buf_len);
