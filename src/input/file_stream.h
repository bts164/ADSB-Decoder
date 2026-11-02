#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <coro/coro_stream.h>

#include "dsp/iq_block.h"

namespace argparse {
class ArgumentParser;
}

/**
 * @file
 * `adsb file` source: replays a SigMF recording, or a raw cf32_le file.
 */

/**
 * Streams a recording as IqBuffers, in the format it is stored in.
 *
 * Reads run as a task on the coro runtime, a few chunks ahead of the consumer, so file IO overlaps demod
 * compute and memory use is bounded by the chunk size rather than the file size.
 * @param path interleaved I/Q file, such as a .sigmf-data
 * @param format how its samples are stored: any of iq::Formats, each read straight into the blocks
 * @param pad guard padding to allocate around each block (demod_padding())
 * @param chunk_samples samples per block; the last block may be shorter
 */
coro::CoroStream<IqBuffer> file_iq_stream(std::string path, iq::Formats::Format format, IqPadding pad,
                                          uint32_t chunk_samples);

/** `adsb file`'s options, and what the recording's SigMF metadata says. */
struct FileStreamParams {
    std::string iq_path;
    iq::Formats::Format format;
    double rate_hz;
    double freq_hz;  /**< labels the published spectrum frames */
    std::optional<double> full_scale = {};  /**< the metadata's adsb:full_scale, for a float format */

    static void add_options(argparse::ArgumentParser& cmd);
    /**
     * Reads the metadata file too: --rate and --freq override what it gives, with a WARNING when they differ.
     * Without one, the file is raw cf32_le, with a WARNING.
     * @throws std::runtime_error if --meta doesn't exist, the metadata can't be read, or it names a format
     *         file_iq_stream() can't read
     */
    static FileStreamParams from_options(const argparse::ArgumentParser& program,
                                        const argparse::ArgumentParser& cmd);

    /**
     * Block size for file_iq_stream(): matches librtlsdr's default async transfer buffer (16*32*512 bytes =
     * 131072 IQ pairs), so every source produces similar blocks.
     */
    static constexpr uint32_t kChunkSamples = 128 * 1024;
};
