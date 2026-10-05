// Tests for BlockStitcher: each block's guard padding is filled from its neighbors, as if the stream were one
// array, but only across contiguous blocks.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "dsp/iq_block.h"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

constexpr IqPadding kPad{3, 4};

// A block of n samples whose values are their stream indices (+1, so 0 means "padding left zero").
static IqBlock ramp(size_t n, uint64_t start) {
    IqBlock b = IqBlock::zeroed(kPad, n, start);
    for (size_t i = 0; i < n; i++) b.iq()[2 * i] = static_cast<float>(start + i + 1);
    return b;
}

// The whole buffer, lead through trail, as the stream values it holds (0 where the padding stayed zero).
static std::vector<int> contents(const IqBlock& b) {
    std::vector<int> v;
    for (size_t i = 0; i < b.total(); i++) v.push_back(static_cast<int>(b.samples(i, 0)));
    return v;
}

using V = std::vector<int>;

static void test_contiguous() {
    BlockStitcher st;
    CHECK(!st.push(ramp(5, 0)));
    auto a = st.push(ramp(5, 5));
    CHECK(a && a->start == 0);
    CHECK(contents(*a) == (V{0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
    auto b = st.push(ramp(5, 10));
    CHECK(contents(*b) == (V{3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}));
    auto c = st.flush();
    CHECK(c && contents(*c) == (V{8, 9, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0}));
    CHECK(!st.flush());
}

// A block shorter than the lead: the next block's lead reaches back through it into the one before, and it
// fills only part of the previous block's trail.
static void test_short_block() {
    BlockStitcher st;
    st.push(ramp(6, 0));
    auto a = st.push(ramp(2, 6));
    CHECK(contents(*a) == (V{0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0}));
    auto b = st.push(ramp(5, 8));
    CHECK(contents(*b) == (V{4, 5, 6, 7, 8, 9, 10, 11, 12}));
    auto c = st.flush();
    CHECK(contents(*c) == (V{6, 7, 8, 9, 10, 11, 12, 13, 0, 0, 0, 0}));
}

// A gap (e.g. a dropped block) leaves the padding on both sides of it zero.
static void test_gap() {
    BlockStitcher st;
    st.push(ramp(5, 0));
    auto a = st.push(ramp(5, 10));
    CHECK(contents(*a) == (V{0, 0, 0, 1, 2, 3, 4, 5, 0, 0, 0, 0}));
    auto b = st.flush();
    CHECK(contents(*b) == (V{0, 0, 0, 11, 12, 13, 14, 15, 0, 0, 0, 0}));
}

int main() {
    test_contiguous();
    test_short_block();
    test_gap();
    std::puts("block_stitcher_test: all passed");
}
