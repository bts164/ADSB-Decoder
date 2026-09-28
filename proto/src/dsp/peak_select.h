#pragma once

#include <vector>

/**
 * Greedy non-maximum suppression over a preamble-score array.
 *
 * Repeatedly accepts the highest-scoring remaining index with score >= `min_score`, then discards every
 * index within `radius` of it (inclusive, either side). Ties go to the lower index.
 * @param score per-position preamble scores
 * @param n number of scores
 * @param min_score acceptance threshold
 * @param radius exclusion radius, in positions
 * @return the accepted indices, ascending
 */
std::vector<int> select_preamble_peaks(const float* score, int n, float min_score, int radius);
