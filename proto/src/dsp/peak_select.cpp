#include "dsp/peak_select.h"

#include <algorithm>

std::vector<int> select_preamble_peaks(const float* score, int n, float min_score, int radius) {
    std::vector<int> above;
    for (int i = 0; i < n; i++) {
        if (score[i] >= min_score) above.push_back(i);
    }
    std::sort(above.begin(), above.end(), [&](int a, int b) {
        return score[a] != score[b] ? score[a] > score[b] : a < b;
    });
    std::vector<char> excluded(static_cast<size_t>(std::max(n, 0)), 0);
    std::vector<int> picks;
    for (int i : above) {
        if (excluded[static_cast<size_t>(i)]) continue;
        picks.push_back(i);
        std::fill(excluded.begin() + std::max(0, i - radius), excluded.begin() + std::min(n, i + radius + 1), 1);
    }
    std::sort(picks.begin(), picks.end());
    return picks;
}
