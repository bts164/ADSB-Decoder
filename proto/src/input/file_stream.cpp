#include "input/file_stream.h"

#include <complex>
#include <cstddef>
#include <iostream>
#include <vector>

#include <coro/io/file.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/mpsc.h>

namespace {

// How many chunks the reader may run ahead of the demod loop. Enough to hide
// read-latency jitter; each is ~1 MB at the default chunk size.
constexpr size_t kReadAheadBlocks = 4;

// coro::ByteBuffer view over the sample region [lead, lead + n) of an
// IqBlock it owns, so File::read_exact() writes straight into the
// guard-padded block instead of a separate buffer that then gets copied.
struct BlockReadBuf {
    IqBlock block;

    std::byte* begin() { return reinterpret_cast<std::byte*>(block.samples.data() + block.lead); }
    std::byte* end() { return begin() + block.n * sizeof(std::complex<float>); }
};

// Reads the file chunk by chunk and sends each chunk down `tx`, running on the
// coro runtime so reads overlap the demod loop's compute. Owns `tx`, so the
// channel closes (ending the consumer's stream) when this returns or throws.
coro::Coro<void> read_loop(std::string path, IqPadding pad, uint32_t chunk_samples, coro::MpscSender<IqBlock> tx) {
    auto file = co_await coro::File::open(path, coro::FileMode::Read);

    std::cerr << "streaming from IQ file " << path << " (chunk=" << chunk_samples << " samples)\n";

    for (uint64_t start = 0;;) {
        BlockReadBuf buf{IqBlock::zeroed(pad, chunk_samples, start)};

        auto [bytes_read, filled] = co_await file.read_exact(std::move(buf));
        if (bytes_read == 0) break;

        // Truncates a stray partial trailing sample the same way the old
        // whole-file loader's byte-count division did. The unread tail of a
        // short last chunk is already zero, so it serves as trailing guard.
        IqBlock block = std::move(filled.block);
        block.n = bytes_read / sizeof(std::complex<float>);
        if (bytes_read % sizeof(std::complex<float>) != 0) block.data()[block.n] = {};
        block.samples.resize(static_cast<size_t>(pad.lead) + block.n + static_cast<size_t>(pad.trail));
        start += block.n;

        if (!co_await tx.send(std::move(block))) break;  // consumer gone

        if (bytes_read < chunk_samples * sizeof(std::complex<float>)) break;  // short read -- EOF reached mid-chunk
    }
}

}  // namespace

coro::CoroStream<IqBlock> file_iq_stream(std::string path, IqPadding pad, uint32_t chunk_samples) {
    auto [tx, rx] = coro::mpsc_channel<IqBlock>(kReadAheadBlocks);

    auto reader = coro::spawn(read_loop(std::move(path), pad, chunk_samples, std::move(tx)));

    while (auto block = co_await coro::next(rx)) {
        co_yield std::move(*block);
    }
    co_await reader;  // rethrows a read_loop failure (e.g. open failure)
}
