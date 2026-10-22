#pragma once

#include <cstdint>
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
 * Streams IqBuffers from an RTL-SDR until the stream is dropped. If the device is unplugged the stream pauses until
 * it has reconnected (RtlSdr::read_async), then resumes with block starts advanced by the samples missed.
 *
 * librtlsdr's own thread (inside rtlsdr_read_async) delivers raw uint8 I/Q buffers to a callback, which
 * converts each one to a guard-padded IqBuffer and pushes it into a small channel this stream drains. One
 * block per librtlsdr buffer. If the demod falls behind and the channel fills, blocks are dropped rather
 * than stalling librtlsdr's USB thread.
 *
 * The stream opens and tunes the device itself (RtlSdr), and owns it. Its samples are labeled with
 * `params.tuning.rate_hz`, which the device's rate is within 100 ppm of.
 * @param params the device, its tuning and librtlsdr's buffers
 * @param pad guard padding to allocate around each block (demod_padding())
 * @throws std::runtime_error from the stream, before its first block, if the device can't be opened, rejects the
 *         rate or frequency, or rounds the rate by more than 100 ppm
 */
coro::CoroStream<IqBuffer> rtlsdr_iq_stream(RtlSdrStreamParams params, IqPadding pad);
