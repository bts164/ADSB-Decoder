#include "dsp/peak_select.h"

#include <algorithm>
#include <vector>

#include <xtensor/xadapt.hpp>
#include <xtensor/xbuilder.hpp>
#include <xtensor/xview.hpp>

xt::xtensor<int, 1> select_preamble_peaks(std::span<const float> score, float min_score, int radius) {
    const int n = static_cast<int>(score.size());
    // How many scores clear the threshold, and how many of those survive, isn't known up front, so both lists
    // grow as vectors; the picks are copied out as a tensor at the end.
    std::vector<int> above;
    for (int i = 0; i < n; i++) {
        if (score[i] >= min_score) above.push_back(i);
    }
    std::sort(above.begin(), above.end(), [&](int a, int b) {
        return score[a] != score[b] ? score[a] > score[b] : a < b;
    });
    xt::xtensor<bool, 1> excluded = xt::zeros<bool>({score.size()});
    std::vector<int> picks;
    for (int i : above) {
        if (excluded(static_cast<size_t>(i))) continue;
        picks.push_back(i);
        xt::view(excluded, xt::range(std::max(0, i - radius), std::min(n, i + radius + 1))) = true;
    }
    std::sort(picks.begin(), picks.end());
    return xt::adapt(picks);
}
