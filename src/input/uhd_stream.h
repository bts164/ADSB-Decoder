#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <coro/coro_stream.h>

#include "dsp/iq_block.h"

namespace argparse {
class ArgumentParser;
}

/**
 * @file
 * `adsb uhd` source: streams from an Ettus USRP through UHD. UHD's headers bring Boost, so they stay in
 * uhd_stream.cpp.
 */

/** `adsb uhd`'s options: device selection and tuning. */
struct UhdStreamParams {
    /** Block size for uhd_iq_stream(), the same as FileStreamParams::kChunkSamples. */
    static constexpr size_t kChunkSamples = 128 * 1024;

    std::string args;                   ///< UHD device address, such as "type=b200" or "addr=192.168.10.2"
    std::optional<std::string> subdev;  ///< receive subdevice spec, such as "A:A"; unset for UHD's default
    size_t channel;                     ///< receive channel, of those the subdevice spec gives
    std::optional<std::string> antenna; ///< such as "RX2"; unset for UHD's default
    double rate_hz;
    std::optional<double> bandwidth_hz; ///< analog bandwidth; unset for UHD's default
    double freq_hz;
    std::optional<double> gain_db;      ///< manual gain; unset for the device's AGC

    static void add_options(argparse::ArgumentParser& cmd);
    /** @throws std::runtime_error if --gain isn't auto or a number */
    static UhdStreamParams from_options(const argparse::ArgumentParser& program, const argparse::ArgumentParser& cmd);
};

/**
 * Streams IqBuffers from a USRP until the stream is dropped.
 *
 * A spawn_blocking thread opens the device, sets subdevice, antenna, rate, frequency, bandwidth and gain, waits for
 * the LO to lock, makes the receive streamer, then logs the device and the settings in effect. A frequency or
 * bandwidth that differs from the one asked for is logged as a warning, and so is a rate within 100 ppm of it; the
 * samples are labeled with `params.rate_hz` either way. UHD's own log messages go to the log, not the console. Its
 * fast-path overflow marks ("O" or "D") still go to stderr, as UHD prints them directly; each overflow is also
 * logged.
 *
 * The thread then reads, gathering the packets into guard-padded blocks of `chunk_samples` pushed into a small channel
 * the stream drains. Samples the device lost, which show as a jump in the packets' times, are filled with zeros, so
 * stream time keeps pace with the device's. If the demod falls behind and the channel fills, blocks are dropped rather
 * than stalling the read, which would make the device overflow. Dropping the stream cancels the read; the thread then
 * stops streaming and releases the device.
 * @param params the device and its tuning
 * @param pad guard padding to allocate around each block (demod_padding())
 * @param chunk_samples real samples per block
 * @throws std::runtime_error from the stream, after its last block, if no device matches the args, the channel or
 *         antenna doesn't exist, the rate is more than 100 ppm from the one asked for, the device has no AGC and no
 *         gain was given, a gain is outside the device's range, the LO doesn't lock, or receiving fails
 */
coro::CoroStream<IqBuffer> uhd_iq_stream(UhdStreamParams params, IqPadding pad, size_t chunk_samples);
