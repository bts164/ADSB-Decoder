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

#include <coro/coro_stream.h>
#include <coro/sync/mpsc.h>
#include <xtensor/xtensor.hpp>

#include "dsp/iq_format.h"

/**
 * @file
 * The IQ every source yields and every stage reads. A source fills an IqBuffer, which it owns alone and can write.
 * stitch() then fills each buffer's guard padding from its neighbors, so demodulate() sees continuous signal across
 * block boundaries, and turns it into an IqBlock: read-only, and shared rather than copied by the stages that read
 * it.
 */

/** Guard padding, in samples, around a block's real samples. demod_padding() gives the sizes it needs. */
struct IqPadding {
    int lead = 0;   /**< samples before the first real sample */
    int trail = 0;  /**< samples after the last real sample */
};

/**
 * Where a block's buffer goes back to once nothing holds it, so its source can reuse it. Shared by every block of
 * the source.
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
 * One block of raw (pre-resample) IQ samples plus guard padding, as a source builds it. A source allocates the
 * padding up front and leaves it zeroed, and writes the real samples; BlockStitcher then fills the padding with
 * the neighboring blocks' samples. Move-only, so whoever holds it is its only writer.
 *
 * The samples are in whichever of the iq::Formats (iq_format.h) the source produced them in, and a source knows
 * its format, so the accessors are typed: iq<F>() and padded<F>().
 *
 * Whatever the format, the samples are a (samples, 2) array, one row per sample with I in column 0 and Q in
 * column 1, rather than an array of std::complex: xtensor leaves arithmetic types uninitialized on allocation
 * but zero-fills std::complex, which at 100+ Msps is a whole extra pass over memory for samples that are
 * about to be overwritten anyway. Rows are contiguous, so iq<F>() and padded<F>() are the interleaved I/Q
 * components the kernels and the sources read and write.
 *
 * A buffer from an IqBufferPool goes back to it when destroyed, or, once it is an IqBlock, when its last share
 * is dropped (best effort: if the pool is full or the source has gone, it is just freed).
 */
class IqBuffer {
public:
    IqBuffer() = default;
    IqBuffer(const IqBuffer&) = delete;
    IqBuffer& operator=(const IqBuffer&) = delete;
    IqBuffer(IqBuffer&&) noexcept = default;
    IqBuffer& operator=(IqBuffer&& other) noexcept {
        if (this != &other) {
            recycle();
            m_n = other.m_n;
            m_lead = other.m_lead;
            m_start = other.m_start;
            m_pool = std::move(other.m_pool);
            m_samples = std::move(other.m_samples);
        }
        return *this;
    }
    ~IqBuffer() { recycle(); }

    /**
     * Allocates a zeroed buffer of `n` samples in format F with `pad` around them.
     * @param pad guard padding to allocate
     * @param n number of real samples
     * @param start stream sample index of the first real sample
     */
    template<class F>
    static IqBuffer zeroed(IqPadding pad, size_t n, uint64_t start);
    /**
     * As zeroed(), but only the padding is zeroed: the `n` real samples are left uninitialized, for a source
     * that is about to write all of them.
     */
    template<class F>
    static IqBuffer uninitialized(IqPadding pad, size_t n, uint64_t start);
    /**
     * As uninitialized(), but for a buffer that goes back to `pool` when nothing holds it. Takes `samples`, one
     * of the pool's, if it holds format F and is the size needed; otherwise it allocates, and `samples` is
     * freed. The real samples are then whatever the buffer last held.
     * @param pool where the buffer goes when nothing holds it, or null to free it
     * @param samples a buffer from `pool`, or empty to allocate
     */
    template<class F>
    static IqBuffer uninitialized(IqPadding pad, size_t n, uint64_t start, IqBufferPool pool,
                                  iq::SamplesVariant&& samples);

    /**
     * Cuts the buffer down to its first `new_n` real samples, keeping the lead and the size of the trailing
     * padding, which is zeroed. Reallocates and copies, so it is for the short last block of a stream.
     * @throws std::invalid_argument if new_n > n()
     */
    void truncate(size_t new_n);

    /** Number of real samples. */
    size_t n() const { return m_n; }
    /** Index of the first real sample in the buffer. */
    int lead() const { return m_lead; }
    /** Stream sample index of the first real sample. */
    uint64_t start() const { return m_start; }

    /** Whether the samples are in format F. */
    template<class F>
    bool holds() const { return std::holds_alternative<iq::Samples<F>>(m_samples); }

    /**
     * The real samples as interleaved I/Q: 2 * n() components.
     * @throws std::bad_variant_access if the buffer is not in format F
     */
    template<class F>
    std::span<typename F::Sample> iq() { return padded<F>().subspan(2 * static_cast<size_t>(m_lead), 2 * m_n); }
    /** @copydoc iq() */
    template<class F>
    std::span<const typename F::Sample> iq() const {
        return padded<F>().subspan(2 * static_cast<size_t>(m_lead), 2 * m_n);
    }
    /**
     * Every sample, padding included, as interleaved I/Q: 2 * total() components.
     * @throws std::bad_variant_access if the buffer is not in format F
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

    /** Number of samples allocated: lead() + n() + trail(). */
    size_t total() const {
        return std::visit([](const auto& s) { return s.data.shape(0); }, m_samples);
    }
    /** Number of samples after the last real sample. */
    size_t trail() const { return total() - static_cast<size_t>(m_lead) - m_n; }

private:
    friend class BlockStitcher;
    friend class IqBlock;

    void recycle() noexcept {
        // A moved-from buffer has no pool (moving a shared_ptr nulls it), so its samples aren't sent back.
        if (m_pool) (void)m_pool->try_send(std::move(m_samples));
        m_pool.reset();
    }

    size_t m_n = 0;
    int m_lead = 0;
    uint64_t m_start = 0;
    IqBufferPool m_pool;           // where the buffer goes when nothing holds it, or null to free it
    iq::SamplesVariant m_samples;  // rows [lead][n real samples][trail], columns I and Q
};

/**
 * A finished block, its padding filled: what stitch() yields and every stage after it reads. Its samples are
 * read-only and held by reference count, so a stage that needs a block another also reads takes a share() of
 * it rather than a copy, and the buffer goes back to its pool when the last share is dropped. Never copied
 * implicitly: share() and clone() say which is meant.
 *
 * A stage that takes blocks from any source reads them with visit(), or to_float() for a run of samples; the
 * typed accessors throw for any format but the block's.
 */
class IqBlock {
public:
    /** Takes `buffer`'s samples, as final: stitch() makes each block this way. */
    explicit IqBlock(IqBuffer&& buffer);

    IqBlock(const IqBlock&) = delete;
    IqBlock& operator=(const IqBlock&) = delete;
    IqBlock(IqBlock&&) noexcept = default;
    IqBlock& operator=(IqBlock&&) noexcept = default;

    /** Another reference to the same samples. */
    IqBlock share() const { return IqBlock(m_n, m_lead, m_start, m_samples); }
    /** A copy of the samples, which has no pool. */
    IqBlock clone() const;

    /** Number of real samples. */
    size_t n() const { return m_n; }
    /** Index of the first real sample in the buffer. */
    int lead() const { return m_lead; }
    /** Stream sample index of the first real sample. */
    uint64_t start() const { return m_start; }

    /** Whether the samples are in format F. */
    template<class F>
    bool holds() const { return std::holds_alternative<iq::Samples<F>>(*m_samples); }

    /**
     * The real samples as interleaved I/Q: 2 * n() components.
     * @throws std::bad_variant_access if the block is not in format F
     */
    template<class F>
    std::span<const typename F::Sample> iq() const {
        return padded<F>().subspan(2 * static_cast<size_t>(m_lead), 2 * m_n);
    }
    /**
     * Every sample, padding included, as interleaved I/Q: 2 * total() components.
     * @throws std::bad_variant_access if the block is not in format F
     */
    template<class F>
    std::span<const typename F::Sample> padded() const {
        const auto& data = std::get<iq::Samples<F>>(*m_samples).data;
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
                return std::forward<Visitor>(visitor)(IqView<F>{
                    std::span<const typename F::Sample>(s.data.data(), s.data.size()), m_n, m_lead, m_start});
            },
            *m_samples);
    }

    /**
     * Converts out.size() / 2 samples starting at buffer index `first` (0 is the first sample of the leading
     * padding, lead() the first real one) to interleaved I/Q floats scaled to [-1, 1], whatever the format.
     * For the consumers that want a run of samples as floats; the resampler reads the block's own format.
     * @throws std::out_of_range if the samples are not all inside the buffer
     * @throws std::invalid_argument if `out` has an odd size
     */
    void to_float(size_t first, std::span<float> out) const;

    /** Number of samples allocated: lead() + n() + trail(). */
    size_t total() const {
        return std::visit([](const auto& s) { return s.data.shape(0); }, *m_samples);
    }
    /** Number of samples after the last real sample. */
    size_t trail() const { return total() - static_cast<size_t>(m_lead) - m_n; }

private:
    IqBlock(size_t n, int lead, uint64_t start, std::shared_ptr<const iq::SamplesVariant> samples)
        : m_n(n), m_lead(lead), m_start(start), m_samples(std::move(samples)) {}

    size_t m_n = 0;
    int m_lead = 0;
    uint64_t m_start = 0;
    std::shared_ptr<const iq::SamplesVariant> m_samples;  // as IqBuffer's
};

template<class F>
IqBuffer IqBuffer::uninitialized(IqPadding pad, size_t n, uint64_t start) {
    return uninitialized<F>(pad, n, start, nullptr, iq::SamplesVariant{});
}

template<class F>
IqBuffer IqBuffer::uninitialized(IqPadding pad, size_t n, uint64_t start, IqBufferPool pool,
                                 iq::SamplesVariant&& samples) {
    IqBuffer b;
    const size_t lead = static_cast<size_t>(pad.lead);
    const std::array<size_t, 2> shape{lead + n + static_cast<size_t>(pad.trail), 2};
    // A truncated block's buffer is shorter, so it doesn't match and is freed.
    const auto* reused = std::get_if<iq::Samples<F>>(&samples);
    if (reused && reused->data.shape(0) == shape[0]) {
        b.m_samples = std::move(samples);
    } else {
        b.m_samples = iq::Samples<F>{xt::xtensor<typename F::Sample, 2>::from_shape(shape)};
    }
    b.m_n = n;
    b.m_lead = pad.lead;
    b.m_start = start;
    b.m_pool = std::move(pool);
    std::ranges::fill(b.padded<F>().first(2 * lead), F::zero);
    std::ranges::fill(b.padded<F>().subspan(2 * (lead + n)), F::zero);
    return b;
}

template<class F>
IqBuffer IqBuffer::zeroed(IqPadding pad, size_t n, uint64_t start) {
    IqBuffer b = uninitialized<F>(pad, n, start);
    std::ranges::fill(b.iq<F>(), F::zero);
    return b;
}

/**
 * Delays a buffer stream by one buffer so each one's guard padding can be filled from both neighbors. stitch()
 * runs one over a stream; nothing else needs to.
 *
 * push() holds each buffer until the next one arrives, then copies the head of the new buffer into the held
 * buffer's trailing padding and the tail of the held buffer (reaching back into its own, already filled,
 * leading padding if it is shorter than the lead) into the new buffer's leading padding. Only buffers whose
 * sample ranges meet (`next.start() == prev.start() + prev.n()`) and that are in the same format are joined;
 * across a gap, such as dropped blocks, the padding stays zero. A new buffer shorter than the trailing padding
 * fills only part of it.
 */
class BlockStitcher {
public:
    /**
     * @param next the newest buffer from the source
     * @return the previous buffer, padding filled on both sides, or nullopt for the first buffer
     */
    std::optional<IqBuffer> push(IqBuffer next);

    /** @return the held buffer (trailing padding left zero, since nothing follows it), or nullopt if none */
    std::optional<IqBuffer> flush();

private:
    std::optional<IqBuffer> m_held;
};

/**
 * A source's buffers as finished blocks, each with its padding filled from its neighbors (BlockStitcher). Each
 * block is held until the next arrives, so the output runs one block behind the input.
 */
coro::CoroStream<IqBlock> stitch(coro::CoroStream<IqBuffer> in);
