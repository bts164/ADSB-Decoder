#include "dsp/iq_block.h"

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <utility>

#include <coro/coro.h>

namespace {

// Samples [first, first + count) of a block's buffer, padding included, as interleaved I/Q. A span and not an
// xt::view, so that copying one is a single memmove: assigning one view to another copies twice, by way of a
// heap-allocated temporary.
template<class F>
std::span<typename F::Sample> rows(iq::Samples<F>& s, size_t first, size_t count) {
    return std::span<typename F::Sample>(s.data.data(), s.data.size()).subspan(2 * first, 2 * count);
}

// Deletes an IqBlock's samples once its last share is dropped, handing them back to the pool first if it has one.
struct Recycle {
    IqBufferPool pool;

    void operator()(const iq::SamplesVariant* samples) const noexcept {
        // No share is left to read the samples, and IqBlock(IqBuffer&&) allocated them non-const, so they can
        // be moved out.
        auto* owned = const_cast<iq::SamplesVariant*>(samples);
        if (pool) (void)pool->try_send(std::move(*owned));
        delete owned;
    }
};

}  // namespace

void IqBuffer::truncate(size_t new_n) {
    if (new_n > m_n) throw std::invalid_argument("IqBuffer::truncate: new_n is past the buffer's samples");
    const size_t keep = static_cast<size_t>(m_lead) + new_n;
    const size_t new_total = keep + trail();
    std::visit(
        [&]<class F>(iq::Samples<F>& s) {
            iq::Samples<F> cut{xt::xtensor<typename F::Sample, 2>::from_shape(std::array<size_t, 2>{new_total, 2})};
            std::ranges::copy(rows(s, 0, keep), rows(cut, 0, keep).begin());
            std::ranges::fill(rows(cut, keep, new_total - keep), F::zero);
            s = std::move(cut);
        },
        m_samples);
    m_n = new_n;
}

void IqBlock::to_float(size_t first, std::span<float> out) const {
    if (out.size() % 2 != 0) throw std::invalid_argument("IqBlock::to_float: odd number of floats");
    visit([&]<class F>(IqView<F> v) {
        if (2 * first > v.padded.size() || out.size() > v.padded.size() - 2 * first)
            throw std::out_of_range("IqBlock::to_float: samples outside the block");
        const auto in = v.padded.subspan(2 * first, out.size());
        constexpr float scale = 1.0f / F::full_scale;
        for (size_t i = 0; i < out.size(); i++) out[i] = F::to_float(in[i]) * scale;
    });
}

IqBlock::IqBlock(IqBuffer&& buffer)
    : m_n(buffer.m_n),
      m_lead(buffer.m_lead),
      m_start(buffer.m_start),
      // Taking the pool leaves the buffer none, so its destructor doesn't send the moved-from samples back.
      m_samples(new iq::SamplesVariant(std::move(buffer.m_samples)), Recycle{std::move(buffer.m_pool)}) {}

IqBlock IqBlock::clone() const {
    return IqBlock(m_n, m_lead, m_start, std::make_shared<const iq::SamplesVariant>(*m_samples));
}

std::optional<IqBuffer> BlockStitcher::push(IqBuffer next) {
    std::optional<IqBuffer> prev = std::exchange(m_held, std::move(next));
    if (!prev) return std::nullopt;
    IqBuffer& cur = *m_held;
    if (cur.m_start != prev->m_start + prev->m_n) return prev;
    std::visit(
        [&]<class F>(iq::Samples<F>& p) {
            // A change of format is treated like a gap: the padding on both sides of it stays zero.
            iq::Samples<F>* const c = std::get_if<iq::Samples<F>>(&cur.m_samples);
            if (!c) return;
            // prev's samples [0, lead + n) are its lead (filled from the block before it) then its own
            // samples, so its tail can supply up to all of cur's lead even when prev is short.
            const size_t prev_end = static_cast<size_t>(prev->m_lead) + prev->m_n;
            const size_t cur_lead = static_cast<size_t>(cur.m_lead);
            const size_t head = std::min(prev->trail(), cur.m_n);
            std::ranges::copy(rows(*c, cur_lead, head), rows(p, prev_end, head).begin());
            const size_t tail = std::min(cur_lead, prev_end);
            std::ranges::copy(rows(p, prev_end - tail, tail), rows(*c, cur_lead - tail, tail).begin());
        },
        prev->m_samples);
    return prev;
}

std::optional<IqBuffer> BlockStitcher::flush() { return std::exchange(m_held, std::nullopt); }

coro::CoroStream<IqBlock> stitch(coro::CoroStream<IqBuffer> in) {
    BlockStitcher stitcher;
    while (auto buffer = co_await coro::next(in)) {
        if (auto ready = stitcher.push(std::move(*buffer))) co_yield IqBlock(std::move(*ready));
    }
    if (auto last = stitcher.flush()) co_yield IqBlock(std::move(*last));
}
