#include "input/file_stream.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <span>
#include <stdexcept>

#include <argparse/argparse.hpp>
#include <coro/coro.h>
#include <coro/io/file.h>
#include <coro/runtime/runtime.h>
#include <coro/task/spawn_builder.h>

#include "common/log.h"
#include "input/sigmf_meta.h"

namespace {

// How many chunks the reader may run ahead of the demod loop. Enough to hide
// read-latency jitter; each is ~1 MB at the default chunk size.
constexpr size_t kReadAheadBlocks = 4;

// Each format is one the blocks hold as stored, so a file is read straight into the block.
static_assert(std::endian::native == std::endian::little, "cf32_le is read as native floats");

// coro::ByteBuffer view over the real samples of an IqBuffer it owns, so
// File::read_exact() writes straight into the guard-padded block instead of
// a separate buffer that then gets copied.
template<class Format>
struct BlockReadBuf {
    IqBuffer block;

    std::byte* begin() { return std::as_writable_bytes(block.iq<Format>()).data(); }
    std::byte* end() { return begin() + block.iq<Format>().size_bytes(); }
};

// Reads the file chunk by chunk, one block per chunk.
template<class Format>
coro::CoroStream<IqBuffer> read_blocks(std::string path, IqPadding pad, uint32_t chunk_samples) {
    // One sample in the file: an I and a Q.
    constexpr size_t kBytesPerSample = 2 * sizeof(typename Format::Sample);
    auto file = co_await coro::File::open(path, coro::FileMode::Read);

    for (uint64_t start = 0;;) {
        BlockReadBuf<Format> buf{IqBuffer::uninitialized<Format>(pad, chunk_samples, start)};

        auto [bytes_read, filled] = co_await file.read_exact(std::move(buf));
        if (bytes_read == 0) break;

        // Short read at EOF: cut off the unread tail of the chunk-sized
        // block, along with a stray partial trailing sample.
        IqBuffer block = std::move(filled.block);
        if (const size_t n = bytes_read / kBytesPerSample; n != block.n()) block.truncate(n);
        start += block.n();

        const bool eof = bytes_read < chunk_samples * kBytesPerSample;  // short read -- EOF reached mid-chunk
        co_yield std::move(block);
        if (eof) break;
    }
}

}  // namespace

std::optional<FileFormat> file_format(std::string_view datatype) {
    if (datatype == "cf32_le") return FileFormat::Cf32Le;
    if (datatype == "ci16_be") return FileFormat::Ci16Be;
    return std::nullopt;
}

const char* datatype_name(FileFormat format) {
    return format == FileFormat::Cf32Le ? "cf32_le" : "ci16_be";
}

coro::CoroStream<IqBuffer> file_iq_stream(std::string path, FileFormat format, IqPadding pad, uint32_t chunk_samples) {
    LOGF(INFO, "streaming from IQ file %s (%s, chunk=%u samples)", path, datatype_name(format), chunk_samples);
    coro::CoroStream<IqBuffer> blocks = format == FileFormat::Cf32Le
                                           ? read_blocks<iq::F32>(std::move(path), pad, chunk_samples)
                                           : read_blocks<iq::I16Be>(std::move(path), pad, chunk_samples);
    // A task of its own, so reads overlap the demodulator's compute. Its next() rethrows a read_blocks()
    // failure (e.g. open failure) once the blocks before it are through.
    auto reader = coro::build_task().name("file_read").buffer(kReadAheadBlocks).spawn(std::move(blocks));
    while (auto block = co_await coro::next(reader)) {
        co_yield std::move(*block);
    }
}

void FileStreamParams::add_options(argparse::ArgumentParser& cmd) {
    cmd.add_description("Replay a SigMF recording, or a raw cf32_le IQ file.");
    cmd.add_argument("--iq").required().help("input IQ file (.sigmf-data)");
    cmd.add_argument("--meta").help(
        "SigMF metadata file (.sigmf-meta) to read the sample format, rate and frequency from (default: --iq's "
        ".sigmf-meta sibling, if there is one; without one, the file is read as cf32_le)");
    cmd.add_argument("--rate").scan<'g', double>().help(
        "sample rate in Hz (default: the metadata's, or 2.4e6 without one)");
    cmd.add_argument("--freq").scan<'g', double>().help(
        "center frequency in Hz, used to label published spectrum frames (default: the metadata's, or 1090e6 "
        "without one)");
}

FileStreamParams FileStreamParams::from_options(const argparse::ArgumentParser& /*program*/,
                                                const argparse::ArgumentParser& cmd) {
    FileStreamParams params{.iq_path = cmd.get<std::string>("--iq"), .format = FileFormat::Cf32Le,
                            .rate_hz = 2.4e6, .freq_hz = 1090e6};
    const std::string meta_path = cmd.present("--meta").value_or(sigmf_meta_path(params.iq_path));
    const std::optional<SigmfMeta> meta = read_sigmf_meta(meta_path);
    if (!meta && cmd.is_used("--meta"))
        throw std::runtime_error("SigMF metadata file " + meta_path + " does not exist");
    if (!meta) LOGF(WARNING, "no SigMF metadata file %s: reading %s as raw cf32_le", meta_path, params.iq_path);

    if (meta && meta->datatype) {
        const std::optional<FileFormat> format = file_format(*meta->datatype);
        if (!format)
            throw std::runtime_error(meta_path + ": core:datatype " + *meta->datatype +
                                     " isn't one adsb reads (cf32_le, ci16_be)");
        params.format = *format;
    }
    if (meta && meta->sample_rate_hz) params.rate_hz = *meta->sample_rate_hz;
    if (meta && meta->frequency_hz) params.freq_hz = *meta->frequency_hz;
    if (const std::optional<double> rate = cmd.present<double>("--rate")) {
        if (meta && meta->sample_rate_hz && *rate != *meta->sample_rate_hz)
            LOGF(WARNING, "file --rate %g Hz overrides the metadata's %g Hz", *rate, *meta->sample_rate_hz);
        params.rate_hz = *rate;
    }
    if (const std::optional<double> freq = cmd.present<double>("--freq")) {
        if (meta && meta->frequency_hz && *freq != *meta->frequency_hz)
            LOGF(WARNING, "file --freq %g Hz overrides the metadata's %g Hz", *freq, *meta->frequency_hz);
        params.freq_hz = *freq;
    }
    LOGF(INFO, "%s: %s at %g Hz, center frequency %g Hz (metadata: %s)", params.iq_path,
         datatype_name(params.format), params.rate_hz, params.freq_hz, meta ? meta_path : "none");
    return params;
}
