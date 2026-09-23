#pragma once

#include <cstdint>
#include <string>

#include <coro/coro_stream.h>

#include "iq_block.h"

// --- --iq file replay mode ----------------------------------------------
//
// Constructs a coro::CoroStream<IqBlock> source that reads `path` (a flat
// interleaved-complex<float32> SigMF .sigmf-data file) chunk_samples samples
// at a time via coro::File, guard-padding each chunk the same way
// rtlsdr_iq_stream's callback does (see demod.h's adsb_leading_pad/
// adsb_trailing_pad) so every yielded IqBlock can go straight into
// AdsbDemod::exec(). Reading in chunks rather than loading the whole file
// up front means run_demod_loop (pipeline.h) sees the same block-at-a-time
// shape regardless of source, and memory use is bounded by chunk_samples
// rather than file size.
//
// Like the live rtlsdr source, AdsbDemod::exec() has no state carried
// between blocks (see demod.h), so a preamble that happens to straddle a
// chunk boundary is missed -- an existing tradeoff of the block-at-a-time
// design, not something file mode makes worse.
coro::CoroStream<IqBlock> file_iq_stream(std::string path, int filter_taps, uint32_t chunk_samples);

// Default chunk size, chosen to match librtlsdr's own default async
// transfer buffer (16*32*512 bytes = 131072 IQ sample pairs, see
// rtlsdr_stream.h) purely so the two sources behave similarly, not because
// anything here depends on librtlsdr's buffering.
constexpr uint32_t kFileStreamChunkSamples = 128 * 1024;
