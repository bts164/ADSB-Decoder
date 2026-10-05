#pragma once

#include <span>

#include <xtensor/xtensor.hpp>

/**
 * Greedy non-maximum suppression over a preamble-score array.
 *
 * Repeatedly accepts the highest-scoring remaining index with score >= `min_score`, then discards every
 * index within `radius` of it (inclusive, either side). Ties go to the lower index.
 * @param score per-position preamble scores
 * @param min_score acceptance threshold
 * @param radius exclusion radius, in positions
 * @return the accepted indices, ascending
 */
xt::xtensor<int, 1> select_preamble_peaks(std::span<const float> score, float min_score, int radius);
