// Tests for select_preamble_peaks: strongest-first selection with an exclusion
// radius, as used by AdsbDemod to turn a preamble-score array into candidates.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "dsp/peak_select.h"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

using V = std::vector<int>;

static V pick(const std::vector<float>& s, float min, int r) {
    return select_preamble_peaks(s.data(), static_cast<int>(s.size()), min, r);
}

// A weak ripple ahead of the true peak (the failure mode that motivated this)
// must not win, however close to the threshold crossing it is.
static void test_ripple_before_peak() {
    std::vector<float> s(200, 0.f);
    s[50] = 8;  // ripple, crosses threshold first
    s[92] = 48; // true peak
    CHECK(pick(s, 5, 100) == V{92});
    // A ripple on the right of the peak is dropped too.
    s[50] = 0;
    s[130] = 8;
    CHECK(pick(s, 5, 100) == V{92});
}

static void test_separate_frames() {
    std::vector<float> s(1000, 0.f);
    s[100] = 10;
    s[500] = 30;
    s[900] = 20;
    CHECK(pick(s, 5, 100) == (V{100, 500, 900}));
    // Radius is inclusive: exactly `radius` apart is excluded, radius+1 is not.
    CHECK(pick(s, 5, 400) == (V{500}));
    s[600] = 12;
    CHECK(pick(s, 5, 100) == (V{100, 500, 900}));  // 100 apart: excluded
    s[600] = 0;
    s[601] = 12;
    CHECK(pick(s, 5, 100) == (V{100, 500, 601, 900}));  // 101 apart: kept
}

// The strongest wins even when weaker neighbours are on both sides and a
// chain (a excludes b, b would have excluded c) is involved.
static void test_greedy_chain() {
    std::vector<float> s(400, 0.f);
    s[100] = 10;
    s[180] = 30;
    s[260] = 20;
    // 180 accepted first; 100 and 260 are both within 100 of it.
    CHECK(pick(s, 5, 100) == (V{180}));
    // With a smaller radius they all survive.
    CHECK(pick(s, 5, 70) == (V{100, 180, 260}));
}

static void test_threshold_ties_edges() {
    std::vector<float> s(50, 0.f);
    CHECK(pick(s, 1, 10).empty());
    s[0] = 3;
    s[49] = 3;
    CHECK(pick(s, 3, 10) == (V{0, 49}));  // >= threshold, array edges
    CHECK(pick(s, 3.5f, 10).empty());
    s[0] = s[5] = 4;
    CHECK(pick(s, 3, 10) == (V{0, 49}));  // 0 and 5 tie: lower index wins, 5 excluded
    CHECK(select_preamble_peaks(nullptr, 0, 1, 10).empty());
}

int main() {
    test_ripple_before_peak();
    test_separate_frames();
    test_greedy_chain();
    test_threshold_ties_edges();
    std::puts("all passed");
}
