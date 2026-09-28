#include "dsp/iq_block.h"

#include <algorithm>
#include <utility>

IqBlock IqBlock::zeroed(IqPadding pad, size_t n, uint64_t start) {
    IqBlock b;
    b.samples.assign(static_cast<size_t>(pad.lead) + n + static_cast<size_t>(pad.trail), {0.0f, 0.0f});
    b.n = n;
    b.lead = pad.lead;
    b.start = start;
    return b;
}

std::optional<IqBlock> BlockStitcher::push(IqBlock next) {
    std::optional<IqBlock> prev = std::exchange(held_, std::move(next));
    if (!prev) return std::nullopt;
    IqBlock& cur = *held_;
    if (cur.start == prev->start + prev->n) {
        // prev's samples [0, lead + n) are its lead (filled from the block before it) then its own samples, so
        // its tail can supply up to all of cur's lead even when prev is short.
        const size_t head = std::min(prev->trail(), cur.n);
        std::copy_n(cur.data(), head, prev->data() + prev->n);
        const size_t tail = std::min(static_cast<size_t>(cur.lead), static_cast<size_t>(prev->lead) + prev->n);
        std::copy_n(prev->data() + prev->n - tail, tail, cur.data() - tail);
    }
    return prev;
}

std::optional<IqBlock> BlockStitcher::flush() { return std::exchange(held_, std::nullopt); }
