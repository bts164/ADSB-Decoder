#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>

#include <coro/sync/mpsc.h>
#include <xtensor/xtensor.hpp>

#include "dsp/iq_format.h"

/**
 * @file
 * IqBlock, the unit of IQ every source (file, vita49, rtlsdr) yields, and BlockStitcher, which fills each
 * block's guard padding from its neighbors so demodulate() sees continuous signal across block
 * boundaries.
 */

/** Guard padding, in samples, around an IqBlock's real samples. demod_padding() gives the sizes it needs. */
struct IqPadding {
    int lead = 0;   /**< samples before the first real sample */
    int trail = 0;  /**< samples after the last real sample */
};

/**
 * Where an IqBlock's buffer goes back to when the block is destroyed, so its source can reuse it. Shared by every
 * block of the source.
 */
using IqBufferPool = std::shared_ptr<coro::MpscSender<iq::SamplesVariant>>;

/**
 * A read-only view of an IqBlock whose samples are in format F: what IqBlock::visit() hands its visitor.
 */
template<class F>
struct IqView {
    std::span<const typename F::Sample> padded;  /**< every sample, padding included, as interleaved I/Q */
    size_t n = 0;                                /**< number of real samples */
    int lead = 0;                                /**< index of the first real sample */
    uint64_t start = 0;                          /**< stream sample index of the first real sample */

    /** The real samples as interleaved I/Q: 2 * n components. */
    std::span<const typename F::Sample> iq() const { return padded.subspan(2 * static_cast<size_t>(lead), 2 * n); }
};

/**
 * One block of raw (pre-resample) IQ samples plus guard padding. A source allocates the padding up front and
 * leaves it zeroed; BlockStitcher then fills it with the neighboring blocks' samples.
 *
 * The samples are in whichever of the iq::Formats (iq_format.h) the source produced them in. A source knows
 * its format and uses the typed accessors, iq<F>() and padded<F>(); a consumer that takes blocks from any
 * source uses visit(), or to_float() for a few samples. The sizes and the stream position are the same for
 * every format and are plain members.
 *
 * Whatever the format, the samples are a (samples, 2) array, one row per sample with I in column 0 and Q in
 * column 1, rather than an array of std::complex: xtensor leaves arithmetic types uninitialized on allocation
 * but zero-fills std::complex, which at 100+ Msps is a whole extra pass over memory for samples that are
 * about to be overwritten anyway. Rows are contiguous, so iq<F>() and padded<F>() are the interleaved I/Q
 * components the kernels and the sources read and write.
 *
 * A block with a `pool` hands its buffer back to it when destroyed (best effort: if the pool is full or the
 * source has gone, the buffer is just freed), so whoever ends up with the block recycles it by dropping it. A
 * copy has no pool.
 */
struct IqBlock {
    size_t n = 0;        /**< number of real samples */
    int lead = 0;        /**< index of the first real sample in the buffer */
    uint64_t start = 0;  /**< stream sample index of the first real sample */
    IqBufferPool pool;   /**< where the buffer goes when the block is destroyed, or null to free it */

    IqBlock() = default;
    IqBlock(const IqBlock& other) : n(other.n), lead(other.lead), start(other.start), m_samples(other.m_samples) {}
    IqBlock(IqBlock&&) noexcept = default;
    IqBlock& operator=(const IqBlock& other) {
        if (this != &other) *this = IqBlock(other);
        return *this;
    }
    IqBlock& operator=(IqBlock&& other) noexcept {
        if (this != &other) {
            recycle();
            n = other.n;
            lead = other.lead;
            start = other.start;
            pool = std::move(other.pool);
            m_samples = std::move(other.m_samples);
        }
        return *this;
    }
    ~IqBlock() { recycle(); }

    /**
     * Allocates a zeroed block of `n` samples in format F with `pad` around them.
     * @param pad guard padding to allocate
     * @param n number of real samples
     * @param start stream sample index of the first real sample
     */
    template<class F>
    static IqBlock zeroed(IqPadding pad, size_t n, uint64_t start);
    /**
     * As zeroed(), but only the padding is zeroed: the `n` real samples are left uninitialized, for a source
     * that is about to write all of them.
     */
    template<class F>
    static IqBlock uninitialized(IqPadding pad, size_t n, uint64_t start);
    /**
     * As uninitialized(), but the block takes `buffer` if it holds format F and is the size the block needs, as a
     * buffer from an IqBufferPool does; otherwise it allocates, and `buffer` is freed. The real samples are then
     * whatever the buffer last held.
     */
    template<class F>
    static IqBlock uninitialized(IqPadding pad, size_t n, uint64_t start, iq::SamplesVariant&& buffer);

    /**
     * Cuts the block down to its first `new_n` real samples, keeping the lead and the size of the trailing
     * padding, which is zeroed. Reallocates and copies, so it is for the short last block of a stream.
     * @throws std::invalid_argument if new_n > n
     */
    void truncate(size_t new_n);

    /** Whether the samples are in format F. */
    template<class F>
    bool holds() const { return std::holds_alternative<iq::Samples<F>>(m_samples); }

    /**
     * The real samples as interleaved I/Q: 2 * n components.
     * @throws std::bad_variant_access if the block is not in format F
     */
    template<class F>
    std::span<typename F::Sample> iq() { return padded<F>().subspan(2 * static_cast<size_t>(lead), 2 * n); }
    /** @copydoc iq() */
    template<class F>
    std::span<const typename F::Sample> iq() const {
        return padded<F>().subspan(2 * static_cast<size_t>(lead), 2 * n);
    }
    /**
     * Every sample, padding included, as interleaved I/Q: 2 * total() components.
     * @throws std::bad_variant_access if the block is not in format F
     */
    template<class F>
    std::span<typename F::Sample> padded() {
        auto& data = std::get<iq::Samples<F>>(m_samples).data;
        return {data.data(), data.size()};
    }
    /** @copydoc padded() */
    template<class F>
    std::span<const typename F::Sample> padded() const {
        const auto& data = std::get<iq::Samples<F>>(m_samples).data;
        return {data.data(), data.size()};
    }

    /**
     * Calls `visitor` with an IqView<F> of the block, F being the format it holds, and returns the result.
     * The visitor is called for exactly one F but must accept every format in iq::Formats, normally as a
     * generic lambda: `block.visit([&]<class F>(IqView<F> v) { ... })`.
     */
    template<class Visitor>
    decltype(auto) visit(Visitor&& visitor) const {
        return std::visit(
            [&]<class F>(const iq::Samples<F>& s) -> decltype(auto) {
                return std::forward<Visitor>(visitor)(
                    IqView<F>{std::span<const typename F::Sample>(s.data.data(), s.data.size()), n, lead, start});
            },
            m_samples);
    }

    /**
     * Converts out.size() / 2 samples starting at buffer index `first` (0 is the first sample of the leading
     * padding, `lead` the first real one) to interleaved I/Q floats scaled to [-1, 1], whatever the format.
     * For the few consumers that want a short run of samples as floats; the resampler reads the block's own
     * format.
     * @throws std::out_of_range if the samples are not all inside the buffer
     * @throws std::invalid_argument if `out` has an odd size
     */
    void to_float(size_t first, std::span<float> out) const;

    /** Number of samples allocated: lead + n + trail(). */
    size_t total() const {
        return std::visit([](const auto& s) { return s.data.shape(0); }, m_samples);
    }
    /** Number of samples after the last real sample. */
    size_t trail() const { return total() - static_cast<size_t>(lead) - n; }

private:
    friend class BlockStitcher;

    void recycle() noexcept {
        // A moved-from block has no pool (moving a shared_ptr nulls it), so its buffer isn't sent back.
        if (pool) (void)pool->try_send(std::move(m_samples));
        pool.reset();
    }

    iq::SamplesVariant m_samples;  // rows [lead][n real samples][trail], columns I and Q
};

template<class F>
IqBlock IqBlock::uninitialized(IqPadding pad, size_t n, uint64_t start) {
    return uninitialized<F>(pad, n, start, iq::SamplesVariant{});
}

template<class F>
IqBlock IqBlock::uninitialized(IqPadding pad, size_t n, uint64_t start, iq::SamplesVariant&& buffer) {
    IqBlock b;
    const size_t lead = static_cast<size_t>(pad.lead);
    const std::array<size_t, 2> shape{lead + n + static_cast<size_t>(pad.trail), 2};
    // A truncated block's buffer is shorter, so it doesn't match and is freed.
    const auto* reused = std::get_if<iq::Samples<F>>(&buffer);
    if (reused && reused->data.shape(0) == shape[0]) {
        b.m_samples = std::move(buffer);
    } else {
        b.m_samples = iq::Samples<F>{xt::xtensor<typename F::Sample, 2>::from_shape(shape)};
    }
    b.n = n;
    b.lead = pad.lead;
    b.start = start;
    std::ranges::fill(b.padded<F>().first(2 * lead), F::zero);
    std::ranges::fill(b.padded<F>().subspan(2 * (lead + n)), F::zero);
    return b;
}

template<class F>
IqBlock IqBlock::zeroed(IqPadding pad, size_t n, uint64_t start) {
    IqBlock b = uninitialized<F>(pad, n, start);
    std::ranges::fill(b.iq<F>(), F::zero);
    return b;
}

/**
 * Delays a block stream by one block so each block's guard padding can be filled from both neighbors.
 *
 * push() holds each block until the next one arrives, then copies the head of the new block into the held
 * block's trailing padding and the tail of the held block (reaching back into its own, already filled,
 * leading padding if it is shorter than the lead) into the new block's leading padding. Only blocks whose
 * sample ranges meet (`next.start == prev.start + prev.n`) and that are in the same format are joined;
 * across a gap, such as dropped blocks, the padding stays zero. A new block shorter than the trailing padding
 * fills only part of it.
 */
class BlockStitcher {
public:
    /**
     * @param next the newest block from the source
     * @return the previous block, padding filled on both sides, or nullopt for the first block
     */
    std::optional<IqBlock> push(IqBlock next);

    /** @return the held block (trailing padding left zero, since nothing follows it), or nullopt if none */
    std::optional<IqBlock> flush();

private:
    std::optional<IqBlock> m_held;
};
