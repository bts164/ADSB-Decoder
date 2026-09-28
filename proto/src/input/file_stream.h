#pragma once

#include <cstdint>
#include <string>

#include <coro/coro_stream.h>

#include "dsp/iq_block.h"

/**
 * @file
 * `adsb file` source: replays a SigMF recording.
 */

/**
 * Streams a cf32 SigMF recording as IqBlocks.
 *
 * Reads run as a task on the coro runtime, a few chunks ahead of the consumer, so file IO overlaps demod
 * compute and memory use is bounded by the chunk size rather than the file size.
 * @param path interleaved complex<float> .sigmf-data file
 * @param pad guard padding to allocate around each block (AdsbDemod::padding())
 * @param chunk_samples samples per block; the last block may be shorter
 */
coro::CoroStream<IqBlock> file_iq_stream(std::string path, IqPadding pad, uint32_t chunk_samples);

/**
 * Default block size: matches librtlsdr's default async transfer buffer (16*32*512 bytes = 131072 IQ
 * pairs), so every source produces similar blocks.
 */
constexpr uint32_t kFileStreamChunkSamples = 128 * 1024;
