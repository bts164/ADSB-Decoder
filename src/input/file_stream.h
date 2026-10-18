#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <coro/coro_stream.h>

#include "dsp/iq_block.h"

namespace argparse {
class ArgumentParser;
}

/**
 * @file
 * `adsb file` source: replays a SigMF recording, or a raw cf32_le file.
 */

/** The sample formats file_iq_stream() reads, named as SigMF's core:datatype names them. */
enum class FileFormat {
    Cf32Le,  ///< "cf32_le": little-endian float I and Q, read as the blocks' native floats
    Ci16Be,  ///< "ci16_be": big-endian int16 I and Q, as vita49_send's packets carry them
};

/** The format a SigMF core:datatype names, or std::nullopt if file_iq_stream() can't read it. */
std::optional<FileFormat> file_format(std::string_view datatype);

/** The SigMF core:datatype name of `format`. */
const char* datatype_name(FileFormat format);

/**
 * Streams a recording as IqBlocks, in the format it is stored in.
 *
 * Reads run as a task on the coro runtime, a few chunks ahead of the consumer, so file IO overlaps demod
 * compute and memory use is bounded by the chunk size rather than the file size.
 * @param path interleaved I/Q file, such as a .sigmf-data
 * @param format how its samples are stored
 * @param pad guard padding to allocate around each block (demod_padding())
 * @param chunk_samples samples per block; the last block may be shorter
 */
coro::CoroStream<IqBlock> file_iq_stream(std::string path, FileFormat format, IqPadding pad, uint32_t chunk_samples);

/** `adsb file`'s options, and what the recording's SigMF metadata says. */
struct FileStreamParams {
    std::string iq_path;
    FileFormat format;
    double rate_hz;
    double freq_hz;  /**< labels the published spectrum frames */

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
