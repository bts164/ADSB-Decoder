#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

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
 */
struct IqBlock {
    std::vector<std::complex<float>> samples;  /**< [lead][n real samples][trail] */
    size_t n = 0;                              /**< number of real samples */
    int lead = 0;                              /**< index of the first real sample in `samples` */
    uint64_t start = 0;                        /**< stream sample index of the first real sample */

    /**
     * Allocates a zeroed block of `n` samples with `pad` around them.
     * @param pad guard padding to allocate
     * @param n number of real samples
     * @param start stream sample index of the first real sample
     */
    static IqBlock zeroed(IqPadding pad, size_t n, uint64_t start);

    std::complex<float>* data() { return samples.data() + lead; }              /**< first real sample */
    const std::complex<float>* data() const { return samples.data() + lead; }  /**< first real sample */
    /** Number of samples after the last real sample. */
    size_t trail() const { return samples.size() - static_cast<size_t>(lead) - n; }
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
