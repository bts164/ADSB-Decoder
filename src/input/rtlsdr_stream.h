#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <coro/coro_stream.h>

#include "dsp/iq_block.h"
#include "input/rtlsdr_device.h"

namespace argparse {
class ArgumentParser;
}

/**
 * @file
 * `adsb rtlsdr` source: streams from a live RTL-SDR.
 */

/** `adsb rtlsdr`'s options. */
struct RtlSdrStreamParams {
    RtlSdrTuning tuning;
    std::optional<uint32_t> buf_num;  /**< librtlsdr async transfer buffers; unset: librtlsdr's default */
    std::optional<uint32_t> buf_len;  /**< bytes per transfer buffer; unset: librtlsdr's default */

    static void add_options(argparse::ArgumentParser& cmd);
    static RtlSdrStreamParams from_options(const argparse::ArgumentParser& program,
                                          const argparse::ArgumentParser& cmd);
};

/**
 * Streams IqBlocks from an RTL-SDR until the stream is dropped. If the device is unplugged the stream pauses until it has
 * reconnected (RtlSdr::read_async), then resumes with block starts advanced by the samples missed.
 *
 * librtlsdr's own thread (inside rtlsdr_read_async) delivers raw uint8 I/Q buffers to a callback, which
 * converts each one to a guard-padded IqBlock and pushes it into a small channel this stream drains. One
 * block per librtlsdr buffer. If the demod falls behind and the channel fills, blocks are dropped rather
 * than stalling librtlsdr's USB thread.
 * @param radio the device, opened and tuned; the stream owns it, and its rate (RtlSdr::sample_rate_hz()) is the
 *        stream's
 * @param pad guard padding to allocate around each block (demod_padding())
 * @param buf_num librtlsdr async buffer count (default: librtlsdr's own)
 * @param buf_len librtlsdr async buffer length in bytes, a multiple of 512 (default: librtlsdr's own)
 */
coro::CoroStream<IqBlock> rtlsdr_iq_stream(std::unique_ptr<RtlSdr> radio, IqPadding pad,
                                            std::optional<uint32_t> buf_num = std::nullopt,
                                            std::optional<uint32_t> buf_len = std::nullopt);
