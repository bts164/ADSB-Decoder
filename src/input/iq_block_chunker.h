#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <utility>

#include "dsp/iq_block.h"

/**
 * @file
 * IqBlockChunker, the accumulator that backs vita49_stream.cpp's reorder-to-chunk pipeline:
 * vrt::Assembler::drain() appends reordered, gap-filled samples into it (it satisfies Assembler's Sink
 * concept, vrt_assembler.h), and completed, guard-padded IqBlocks come back out ready to send with no
 * further copying.
 */

/**
 * Accumulates samples directly into fixed, `chunk_samples`-sized IqBlock slots, so a completed block is
 * produced entirely in place: unlike an intermediate ring/linear buffer, there is no separate copy from
 * the accumulator into the block that gets sent.
 *
 * Each slot is allocated once (uninitialized) when a new block starts; only its lead/trail guard padding
 * is zeroed up front (BlockStitcher fills it properly later, but it must read as zero for gaps that never
 * get stitched). The real-sample interior is left uninitialized, since append()/append_zeros() are
 * guaranteed to fully overwrite it before the block is ever handed out via pop().
 */
class IqBlockChunker {
public:
    IqBlockChunker(IqPadding pad, size_t chunk_samples) : m_pad(pad), m_chunk_samples(chunk_samples) {}

    // --- Sink interface for vrt::Assembler::drain()/write() (see vrt_assembler.h's Sink concept) ---

    /** Appends `n` zero samples (gap fill), completing and queuing blocks as they fill. */
    void append_zeros(size_t n);
    /**
     * Appends samples given as interleaved I/Q floats, completing and queuing blocks as they fill.
     * @throws std::invalid_argument if `iq` has an odd number of floats
     */
    void append(std::span<const float> iq);
    /**
     * Appends `n` samples that `write(dst, offset)` writes straight into the blocks, completing and queuing
     * blocks as they fill. Each call gets the interleaved I/Q floats `dst` of the samples starting at
     * `offset` of the `n`, dst.size() / 2 of them.
     */
    template<typename Writer>
    void append_with(size_t n, Writer&& write) {
        fill(n, std::forward<Writer>(write));
    }

    // --- Consumer interface for the receive loop ---

    /** Number of completed blocks waiting to be popped. */
    size_t ready() const { return m_ready.size(); }
    /**
     * Removes and returns the oldest completed block.
     * @throws std::logic_error if ready() == 0
     */
    IqBlock pop();
    /**
     * Queues the in-progress block (if any), shrunk to the samples it actually received, with trailing
     * padding left zero (nothing follows it). A no-op if nothing has been appended since the last flush.
     */
    void flush();

private:
    IqBlock start_block();
    // Shared span-crossing fill loop for append()/append_zeros(): writes `n` samples to the block(s)
    // currently building via `writer(dst, src_offset)`, queuing each block as it fills.
    template<typename Writer>
    void fill(size_t n, Writer&& writer);

    IqPadding m_pad;
    size_t m_chunk_samples;
    uint64_t m_next_start = 0;
    std::optional<IqBlock> m_building;
    size_t m_filled = 0;  // real samples written into m_building so far
    std::deque<IqBlock> m_ready;
};

template<typename Writer>
void IqBlockChunker::fill(size_t n, Writer&& writer) {
    size_t written = 0;
    while (written < n) {
        if (!m_building) m_building = start_block();
        const size_t take = std::min(m_chunk_samples - m_filled, n - written);
        writer(m_building->iq().subspan(2 * m_filled, 2 * take), written);
        m_filled += take;
        written += take;
        if (m_filled == m_chunk_samples) {
            m_ready.push_back(std::move(*m_building));
            m_building.reset();
            m_filled = 0;
        }
    }
}
