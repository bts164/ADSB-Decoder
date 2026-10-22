#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

#include <coro/sync/mpsc.h>

#include "dsp/iq_block.h"

/**
 * @file
 * IqBlockChunker, the accumulator that backs vita49_stream.cpp's reorder-to-chunk pipeline:
 * vrt::Assembler::drain() appends reordered, gap-filled samples into it (it satisfies Assembler's Sink
 * concept, vrt/assembler.h), and completed, guard-padded IqBuffers come back out ready to send with no
 * further copying.
 */

/**
 * Accumulates samples of format F (iq_format.h) directly into fixed, `chunk_samples`-sized IqBuffer slots,
 * so a completed block is produced entirely in place: unlike an intermediate ring/linear buffer, there is
 * no separate copy from the accumulator into the block that gets sent. The samples are stored as they are
 * appended, with no conversion.
 *
 * Each slot is allocated once (uninitialized) when a new block starts; only its lead/trail guard padding
 * is zeroed up front (BlockStitcher fills it properly later, but it must read as zero for gaps that never
 * get stitched). The real-sample interior is left uninitialized, since append()/append_zeros() are
 * guaranteed to fully overwrite it before the block is ever handed out via pop().
 *
 * Given an IqBufferPool, the chunker sets it on every block, and a new block takes a buffer from the pool's
 * receiving end when one is waiting rather than allocating.
 */
template<class F>
class IqBlockChunker {
public:
    using Sample = typename F::Sample;

    IqBlockChunker(IqPadding pad, size_t chunk_samples) : m_pad(pad), m_chunk_samples(chunk_samples) {}
    /**
     * As above, recycling buffers through `pool`.
     * @param pool set on every block, so each hands its buffer back when destroyed
     * @param free the receiving end of `pool`'s channel, which new blocks take their buffers from
     */
    IqBlockChunker(IqPadding pad, size_t chunk_samples, IqBufferPool pool,
                   coro::MpscReceiver<iq::SamplesVariant> free)
        : m_pad(pad), m_chunk_samples(chunk_samples), m_pool(std::move(pool)), m_free(std::move(free)) {}

    // --- Sink interface for vrt::Assembler::drain()/write() (see vrt/assembler.h's Sink concept) ---

    /** Appends `n` zero samples (gap fill), completing and queuing blocks as they fill. */
    void append_zeros(size_t n) {
        fill(n, [](std::span<Sample> dst, size_t /*src_offset*/) { std::ranges::fill(dst, F::zero); });
    }
    /**
     * Appends samples given as interleaved I/Q components, completing and queuing blocks as they fill.
     * @throws std::invalid_argument if `iq` has an odd number of components
     */
    void append(std::span<const Sample> iq) {
        if (iq.size() % 2 != 0) throw std::invalid_argument("IqBlockChunker::append: odd number of components");
        fill(iq.size() / 2, [&](std::span<Sample> dst, size_t src_offset) {
            std::ranges::copy(iq.subspan(2 * src_offset, dst.size()), dst.begin());
        });
    }
    /**
     * Appends `n` samples that `write(dst, offset)` writes straight into the blocks, completing and queuing
     * blocks as they fill. Each call gets the interleaved I/Q components `dst` of the samples starting at
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
    IqBuffer pop() {
        if (m_ready.empty()) throw std::logic_error("IqBlockChunker::pop: no block ready");
        IqBuffer b = std::move(m_ready.front());
        m_ready.pop_front();
        return b;
    }
    /**
     * Queues the in-progress block (if any), shrunk to the samples it actually received, with trailing
     * padding left zero (nothing follows it). A no-op if nothing has been appended since the last flush.
     */
    void flush() {
        if (!m_building) return;
        // The in-progress slot was sized for a full chunk; cut it down to what actually got filled plus
        // trailing padding, which must read as zero since nothing follows.
        m_building->truncate(m_filled);
        m_ready.push_back(std::move(*m_building));
        m_building.reset();
        m_filled = 0;
    }

private:
    IqBuffer start_block() {
        // Only the guard padding is zeroed here: append()/append_zeros() are guaranteed to fully
        // overwrite the real-sample interior before this block is ever queued (fill() only queues it once
        // m_filled reaches m_chunk_samples), so leaving it uninitialized is safe and avoids the cost of
        // zeroing samples that are about to be written anyway. The same holds for a recycled buffer's stale
        // samples.
        iq::SamplesVariant buffer;
        if (m_free) {
            if (auto recycled = m_free->try_recv()) buffer = std::move(*recycled);
        }
        IqBuffer b = IqBuffer::uninitialized<F>(m_pad, m_chunk_samples, m_next_start, m_pool, std::move(buffer));
        m_next_start += m_chunk_samples;
        return b;
    }
    // Shared span-crossing fill loop for append()/append_zeros(): writes `n` samples to the block(s)
    // currently building via `writer(dst, src_offset)`, queuing each block as it fills.
    template<typename Writer>
    void fill(size_t n, Writer&& writer) {
        size_t written = 0;
        while (written < n) {
            if (!m_building) m_building = start_block();
            const size_t take = std::min(m_chunk_samples - m_filled, n - written);
            writer(m_building->template iq<F>().subspan(2 * m_filled, 2 * take), written);
            m_filled += take;
            written += take;
            if (m_filled == m_chunk_samples) {
                m_ready.push_back(std::move(*m_building));
                m_building.reset();
                m_filled = 0;
            }
        }
    }

    IqPadding m_pad;
    size_t m_chunk_samples;
    IqBufferPool m_pool;
    std::optional<coro::MpscReceiver<iq::SamplesVariant>> m_free;
    uint64_t m_next_start = 0;
    std::optional<IqBuffer> m_building;
    size_t m_filled = 0;  // real samples written into m_building so far
    std::deque<IqBuffer> m_ready;
};
