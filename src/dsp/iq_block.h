#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <xtensor/xtensor.hpp>

/**
 * @file
 * IqBlock, the unit of IQ every source (file, vita49, rtlsdr) yields, and BlockStitcher, which fills each
 * block's guard padding from its neighbors so AdsbDemod::exec() sees continuous signal across block
 * boundaries.
 */

/** Guard padding, in samples, around an IqBlock's real samples. AdsbDemod::padding() gives the sizes it needs. */
struct IqPadding {
    int lead = 0;   /**< samples before the first real sample */
    int trail = 0;  /**< samples after the last real sample */
};

/**
 * One block of raw (pre-resample) IQ samples plus guard padding. A source allocates the padding up front and
 * leaves it zeroed; BlockStitcher then fills it with the neighboring blocks' samples.
 *
 * The samples are a (samples, 2) array of floats, one row per sample with I in column 0 and Q in column 1,
 * rather than an array of std::complex: xtensor leaves floats uninitialized on allocation but zero-fills
 * std::complex, which at 100+ Msps is a whole extra pass over memory for samples that are about to be
 * overwritten anyway. Rows are contiguous, so iq() and padded() are the interleaved I/Q floats the kernels
 * and the sources read and write.
 */
struct IqBlock {
    xt::xtensor<float, 2> samples;             /**< rows [lead][n real samples][trail], columns I and Q */
    size_t n = 0;                              /**< number of real samples */
    int lead = 0;                              /**< row of the first real sample in `samples` */
    uint64_t start = 0;                        /**< stream sample index of the first real sample */

    /**
     * Allocates a zeroed block of `n` samples with `pad` around them.
     * @param pad guard padding to allocate
     * @param n number of real samples
     * @param start stream sample index of the first real sample
     */
    static IqBlock zeroed(IqPadding pad, size_t n, uint64_t start);
    /**
     * As zeroed(), but only the padding is zeroed: the `n` real samples are left uninitialized, for a source
     * that is about to write all of them.
     */
    static IqBlock uninitialized(IqPadding pad, size_t n, uint64_t start);

    /**
     * Cuts the block down to its first `new_n` real samples, keeping the lead and the size of the trailing
     * padding, which is zeroed. Reallocates and copies, so it is for the short last block of a stream.
     * @throws std::invalid_argument if new_n > n
     */
    void truncate(size_t new_n);

    /** The real samples as interleaved I/Q: 2 * n floats. */
    std::span<float> iq() { return padded().subspan(2 * static_cast<size_t>(lead), 2 * n); }
    /** The real samples as interleaved I/Q: 2 * n floats. */
    std::span<const float> iq() const { return padded().subspan(2 * static_cast<size_t>(lead), 2 * n); }
    /** Every sample, padding included, as interleaved I/Q: 2 * total() floats. */
    std::span<float> padded() { return {samples.data(), samples.size()}; }
    /** Every sample, padding included, as interleaved I/Q: 2 * total() floats. */
    std::span<const float> padded() const { return {samples.data(), samples.size()}; }

    /** Number of samples allocated: lead + n + trail(). `samples.size()` is twice this. */
    size_t total() const { return samples.shape(0); }
    /** Number of samples after the last real sample. */
    size_t trail() const { return total() - static_cast<size_t>(lead) - n; }
};

/**
 * Delays a block stream by one block so each block's guard padding can be filled from both neighbors.
 *
 * push() holds each block until the next one arrives, then copies the head of the new block into the held
 * block's trailing padding and the tail of the held block (reaching back into its own, already filled,
 * leading padding if it is shorter than the lead) into the new block's leading padding. Only blocks whose
 * sample ranges meet (`next.start == prev.start + prev.n`) are joined; across a gap, such as dropped blocks,
 * the padding stays zero. A new block shorter than the trailing padding fills only part of it.
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
    std::optional<IqBlock> held_;
};
