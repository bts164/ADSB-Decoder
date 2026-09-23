#include "file_stream.h"

#include <complex>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <vector>

#include <coro/io/file.h>

#include "demod.h"

coro::CoroStream<IqBlock> file_iq_stream(std::string path, int filter_taps, uint32_t chunk_samples) {
    auto file = co_await coro::File::open(path, coro::FileMode::Read);

    std::cerr << "streaming from IQ file " << path << " (chunk=" << chunk_samples << " samples)\n";

    const int lead = adsb_leading_pad(filter_taps);
    const int trail = adsb_trailing_pad(filter_taps);
    const size_t chunk_bytes = static_cast<size_t>(chunk_samples) * sizeof(std::complex<float>);

    for (;;) {
        auto [bytes_read, buf] = co_await file.read_exact(std::vector<std::byte>(chunk_bytes));
        if (bytes_read == 0) break;

        // Truncates a stray partial trailing sample the same way the old
        // whole-file loader's byte-count division did.
        const size_t n = bytes_read / sizeof(std::complex<float>);
        IqBlock block;
        block.samples.assign(static_cast<size_t>(lead) + n + static_cast<size_t>(trail),
                              std::complex<float>{0.0f, 0.0f});
        block.n = n;
        block.lead = lead;
        std::memcpy(block.samples.data() + lead, buf.data(), n * sizeof(std::complex<float>));

        co_yield std::move(block);

        if (bytes_read < chunk_bytes) break;  // short read -- EOF reached mid-chunk
    }
}
