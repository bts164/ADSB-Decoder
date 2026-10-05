#include "dsp/iq_block.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

#include <xtensor/xbuilder.hpp>
#include <xtensor/xview.hpp>

namespace {

// `count` uninitialized samples.
xt::xtensor<float, 2> allocate(size_t count) { return xt::empty<float>(std::array<size_t, 2>{count, 2}); }

// Samples [first, first + count) of a block's buffer, padding included.
auto rows(IqBlock& b, size_t first, size_t count) {
    return xt::view(b.samples, xt::range(first, first + count), xt::all());
}

}  // namespace

IqBlock IqBlock::zeroed(IqPadding pad, size_t n, uint64_t start) {
    IqBlock b = uninitialized(pad, n, start);
    std::ranges::fill(b.iq(), 0.0f);
    return b;
}

IqBlock IqBlock::uninitialized(IqPadding pad, size_t n, uint64_t start) {
    IqBlock b;
    const size_t lead = static_cast<size_t>(pad.lead);
    b.samples = allocate(lead + n + static_cast<size_t>(pad.trail));
    b.n = n;
    b.lead = pad.lead;
    b.start = start;
    std::ranges::fill(b.padded().first(2 * lead), 0.0f);
    std::ranges::fill(b.padded().subspan(2 * (lead + n)), 0.0f);
    return b;
}

void IqBlock::truncate(size_t new_n) {
    if (new_n > n) throw std::invalid_argument("IqBlock::truncate: new_n is past the block's samples");
    const size_t keep = static_cast<size_t>(lead) + new_n;
    xt::xtensor<float, 2> cut = allocate(keep + trail());
    const std::span<float> dst(cut.data(), cut.size());
    std::ranges::copy(padded().first(2 * keep), dst.begin());
    std::ranges::fill(dst.subspan(2 * keep), 0.0f);
    samples = std::move(cut);
    n = new_n;
}

std::optional<IqBlock> BlockStitcher::push(IqBlock next) {
    std::optional<IqBlock> prev = std::exchange(held_, std::move(next));
    if (!prev) return std::nullopt;
    IqBlock& cur = *held_;
    if (cur.start == prev->start + prev->n) {
        // prev's samples [0, lead + n) are its lead (filled from the block before it) then its own samples, so
        // its tail can supply up to all of cur's lead even when prev is short.
        const size_t prev_end = static_cast<size_t>(prev->lead) + prev->n;
        const size_t cur_lead = static_cast<size_t>(cur.lead);
        const size_t head = std::min(prev->trail(), cur.n);
        rows(*prev, prev_end, head) = rows(cur, cur_lead, head);
        const size_t tail = std::min(cur_lead, prev_end);
        rows(cur, cur_lead - tail, tail) = rows(*prev, prev_end - tail, tail);
    }
    return prev;
}

std::optional<IqBlock> BlockStitcher::flush() { return std::exchange(held_, std::nullopt); }
