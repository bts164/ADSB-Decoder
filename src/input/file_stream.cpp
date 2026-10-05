#include "input/file_stream.h"

#include <algorithm>
#include <cstddef>
#include <span>

#include <coro/coro.h>
#include <coro/io/file.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/mpsc.h>

#include "common/log.h"

namespace {

// How many chunks the reader may run ahead of the demod loop. Enough to hide
// read-latency jitter; each is ~1 MB at the default chunk size.
constexpr size_t kReadAheadBlocks = 4;

// One sample in the file: a float each for I and Q.
constexpr size_t kBytesPerSample = 2 * sizeof(float);

// coro::ByteBuffer view over the real samples of an IqBlock it owns, so
// File::read_exact() writes straight into the guard-padded block instead of
// a separate buffer that then gets copied.
struct BlockReadBuf {
    IqBlock block;

    std::byte* begin() { return std::as_writable_bytes(block.iq()).data(); }
    std::byte* end() { return begin() + block.iq().size_bytes(); }
};

// Reads the file chunk by chunk and sends each chunk down `tx`, running on the
// coro runtime so reads overlap the demod loop's compute. Owns `tx`, so the
// channel closes (ending the consumer's stream) when this returns or throws.
coro::Coro<void> read_loop(std::string path, IqPadding pad, uint32_t chunk_samples, coro::MpscSender<IqBlock> tx) {
    auto file = co_await coro::File::open(path, coro::FileMode::Read);

    LOGF(INFO, "streaming from IQ file %s (chunk=%u samples)", path, chunk_samples);

    for (uint64_t start = 0;;) {
        BlockReadBuf buf{IqBlock::uninitialized(pad, chunk_samples, start)};

        auto [bytes_read, filled] = co_await file.read_exact(std::move(buf));
        if (bytes_read == 0) break;

        // Short read at EOF: cut off the unread tail of the chunk-sized
        // block, along with a stray partial trailing sample.
        IqBlock block = std::move(filled.block);
        if (const size_t n = bytes_read / kBytesPerSample; n != block.n) block.truncate(n);
        start += block.n;

        if (!co_await tx.send(std::move(block))) break;  // consumer gone

        if (bytes_read < chunk_samples * kBytesPerSample) break;  // short read -- EOF reached mid-chunk
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
