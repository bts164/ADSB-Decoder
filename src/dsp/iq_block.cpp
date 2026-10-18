#include "dsp/iq_block.h"

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <utility>

namespace {

// Samples [first, first + count) of a block's buffer, padding included, as interleaved I/Q. A span and not an
// xt::view, so that copying one is a single memmove: assigning one view to another copies twice, by way of a
// heap-allocated temporary.
template<class F>
std::span<typename F::Sample> rows(iq::Samples<F>& s, size_t first, size_t count) {
    return std::span<typename F::Sample>(s.data.data(), s.data.size()).subspan(2 * first, 2 * count);
}

}  // namespace

void IqBlock::truncate(size_t new_n) {
    if (new_n > n) throw std::invalid_argument("IqBlock::truncate: new_n is past the block's samples");
    const size_t keep = static_cast<size_t>(lead) + new_n;
    const size_t new_total = keep + trail();
    std::visit(
        [&]<class F>(iq::Samples<F>& s) {
            iq::Samples<F> cut{xt::xtensor<typename F::Sample, 2>::from_shape(std::array<size_t, 2>{new_total, 2})};
            std::ranges::copy(rows(s, 0, keep), rows(cut, 0, keep).begin());
            std::ranges::fill(rows(cut, keep, new_total - keep), F::zero);
            s = std::move(cut);
        },
        m_samples);
    n = new_n;
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

std::optional<IqBlock> BlockStitcher::push(IqBlock next) {
    std::optional<IqBlock> prev = std::exchange(m_held, std::move(next));
    if (!prev) return std::nullopt;
    IqBlock& cur = *m_held;
    if (cur.start != prev->start + prev->n) return prev;
    std::visit(
        [&]<class F>(iq::Samples<F>& p) {
            // A change of format is treated like a gap: the padding on both sides of it stays zero.
            iq::Samples<F>* const c = std::get_if<iq::Samples<F>>(&cur.m_samples);
            if (!c) return;
            // prev's samples [0, lead + n) are its lead (filled from the block before it) then its own
            // samples, so its tail can supply up to all of cur's lead even when prev is short.
            const size_t prev_end = static_cast<size_t>(prev->lead) + prev->n;
            const size_t cur_lead = static_cast<size_t>(cur.lead);
            const size_t head = std::min(prev->trail(), cur.n);
            std::ranges::copy(rows(*c, cur_lead, head), rows(p, prev_end, head).begin());
            const size_t tail = std::min(cur_lead, prev_end);
            std::ranges::copy(rows(p, prev_end - tail, tail), rows(*c, cur_lead - tail, tail).begin());
        },
        prev->m_samples);
    return prev;
}

std::optional<IqBlock> BlockStitcher::flush() { return std::exchange(m_held, std::nullopt); }
