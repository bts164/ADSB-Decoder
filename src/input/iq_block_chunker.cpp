#include "input/iq_block_chunker.h"

#include <stdexcept>
#include <utility>

void IqBlockChunker::append_zeros(size_t n) {
    if (n == 0) return;
    fill(n, [](std::span<float> dst, size_t /*src_offset*/) { std::ranges::fill(dst, 0.0f); });
}

void IqBlockChunker::append(std::span<const float> iq) {
    if (iq.size() % 2 != 0) throw std::invalid_argument("IqBlockChunker::append: odd number of floats");
    if (iq.empty()) return;
    fill(iq.size() / 2, [&](std::span<float> dst, size_t src_offset) {
        std::ranges::copy(iq.subspan(2 * src_offset, dst.size()), dst.begin());
    });
}

IqBlock IqBlockChunker::pop() {
    if (m_ready.empty()) throw std::logic_error("IqBlockChunker::pop: no block ready");
    IqBlock b = std::move(m_ready.front());
    m_ready.pop_front();
    return b;
}

void IqBlockChunker::flush() {
    if (!m_building) return;
    // The in-progress slot was sized for a full chunk; cut it down to what actually got filled plus
    // trailing padding, which must read as zero since nothing follows.
    m_building->truncate(m_filled);
    m_ready.push_back(std::move(*m_building));
    m_building.reset();
    m_filled = 0;
}

IqBlock IqBlockChunker::start_block() {
    // Only the guard padding is zeroed here: append()/append_zeros() are guaranteed to fully
    // overwrite the real-sample interior before this block is ever queued (fill() only queues it once
    // m_filled reaches m_chunk_samples), so leaving it uninitialized is safe and avoids the cost of
    // zeroing samples that are about to be written anyway.
    IqBlock b = IqBlock::uninitialized(m_pad, m_chunk_samples, m_next_start);
    m_next_start += m_chunk_samples;
    return b;
}
