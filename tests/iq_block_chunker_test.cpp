// Unit tests for IqBlockChunker (input/iq_block_chunker.h).

#include <algorithm>
#include <cstdio>
#include <span>
#include <stdexcept>
#include <vector>

#include "input/iq_block_chunker.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                               \
        }                                                                               \
    } while (0)

constexpr IqPadding kPad{2, 3};
constexpr size_t kChunk = 4;

// `n` samples as interleaved I/Q floats, I counting up from `first` and Q zero.
std::vector<float> samples(size_t first, size_t n) {
    std::vector<float> v(2 * n, 0.0f);
    for (size_t i = 0; i < n; i++) v[2 * i] = static_cast<float>(first + i);
    return v;
}

// Real samples [0, n) of a block, as a plain vector for comparison against samples(...).
std::vector<float> real_of(const IqBlock& b) { return {b.iq().begin(), b.iq().end()}; }

// Samples [first, first + n) of a block's real samples, as interleaved I/Q floats.
std::span<const float> part(const IqBlock& b, size_t first, size_t n) { return b.iq().subspan(2 * first, 2 * n); }

bool all_zero(std::span<const float> iq) {
    for (float f : iq) {
        if (f != 0.0f) return false;
    }
    return true;
}

// Every completed block must be guard-padded, real-sample-complete, and start-indexed n samples
// after the previous one.
void check_well_formed_block(const IqBlock& b, size_t expect_n, uint64_t expect_start) {
    CHECK(b.n == expect_n);
    CHECK(b.start == expect_start);
    CHECK(b.lead == kPad.lead);
    const size_t lead = static_cast<size_t>(kPad.lead);
    const size_t trail = static_cast<size_t>(kPad.trail);
    CHECK(b.total() == lead + expect_n + trail);
    CHECK(b.padded().size() == 2 * b.total());
    CHECK(all_zero(b.padded().first(2 * lead)));
    CHECK(all_zero(b.padded().last(2 * trail)));
}

void test_completes_one_block() {
    IqBlockChunker c(kPad, kChunk);
    CHECK(c.ready() == 0);
    c.append(samples(100, kChunk));
    CHECK(c.ready() == 1);
    IqBlock b = c.pop();
    check_well_formed_block(b, kChunk, 0);
    CHECK(real_of(b) == samples(100, kChunk));
    CHECK(c.ready() == 0);
}

// A single append() spanning a block boundary must complete the first block and carry the rest
// into the next, still in flight until it too fills.
void test_append_crosses_block_boundary() {
    IqBlockChunker c(kPad, kChunk);
    c.append(samples(0, 6));  // 4 complete block 0, 2 start block 1
    CHECK(c.ready() == 1);
    IqBlock b0 = c.pop();
    check_well_formed_block(b0, kChunk, 0);
    CHECK(real_of(b0) == samples(0, 4));
    CHECK(c.ready() == 0);  // block 1 only half full

    c.append(samples(6, 2));  // completes block 1
    CHECK(c.ready() == 1);
    IqBlock b1 = c.pop();
    check_well_formed_block(b1, kChunk, kChunk);
    CHECK(real_of(b1) == samples(4, 4));
}

// A single append() spanning multiple full blocks at once must queue all of them, in order.
void test_append_spans_multiple_blocks() {
    IqBlockChunker c(kPad, kChunk);
    c.append(samples(0, 9));  // 2 full blocks + 1 sample starting a third
    CHECK(c.ready() == 2);
    IqBlock b0 = c.pop();
    IqBlock b1 = c.pop();
    check_well_formed_block(b0, kChunk, 0);
    check_well_formed_block(b1, kChunk, kChunk);
    CHECK(real_of(b0) == samples(0, 4));
    CHECK(real_of(b1) == samples(4, 4));
    CHECK(c.ready() == 0);  // the 9th sample is only starting a third block
}

void test_append_zeros_fills_real_region() {
    IqBlockChunker c(kPad, kChunk);
    c.append_zeros(kChunk);
    CHECK(c.ready() == 1);
    IqBlock b = c.pop();
    check_well_formed_block(b, kChunk, 0);
    CHECK(all_zero(b.iq()));
}

// append_zeros() and append() must be able to jointly fill a single block (gap fill followed by
// real samples arriving in the same drain() call).
void test_mixed_zeros_then_samples_in_one_block() {
    IqBlockChunker c(kPad, kChunk);
    c.append_zeros(2);
    c.append(samples(50, 2));
    CHECK(c.ready() == 1);
    IqBlock b = c.pop();
    check_well_formed_block(b, kChunk, 0);
    CHECK(all_zero(part(b, 0, 2)));
    CHECK(std::ranges::equal(part(b, 2, 2), samples(50, 2)));
}

void test_zero_length_calls_are_noop() {
    IqBlockChunker c(kPad, kChunk);
    c.append(std::span<const float>{});
    c.append_zeros(0);
    CHECK(c.ready() == 0);
}

void test_pop_on_empty_throws() {
    IqBlockChunker c(kPad, kChunk);
    bool threw = false;
    try {
        c.pop();
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);
}

// flush() must queue a shrunk, correctly guard-padded block for a partially filled slot, with the
// unfilled tail of the original chunk-sized allocation discarded (not exposed as extra samples).
void test_flush_shrinks_partial_block() {
    IqBlockChunker c(kPad, kChunk);
    c.append(samples(7, 3));  // 3 of 4 real samples filled
    CHECK(c.ready() == 0);
    c.flush();
    CHECK(c.ready() == 1);
    IqBlock b = c.pop();
    check_well_formed_block(b, 3, 0);
    CHECK(real_of(b) == samples(7, 3));
}

// flush() with nothing appended, or right after an exact multiple of chunk_samples, must not
// queue a spurious empty block.
void test_flush_noop_when_nothing_building() {
    IqBlockChunker c(kPad, kChunk);
    c.flush();
    CHECK(c.ready() == 0);

    c.append(samples(0, kChunk));  // exactly fills one block, nothing left building
    CHECK(c.ready() == 1);
    c.flush();
    CHECK(c.ready() == 1);  // still just the one block, not two
}

// Block start indices must advance by chunk_samples per completed block, in order, across several
// append() calls whose sizes don't line up with block boundaries.
void test_start_indices_advance_in_order() {
    IqBlockChunker c(kPad, kChunk);
    c.append(samples(0, 5));
    c.append(samples(5, 3));
    c.append(samples(8, 4));
    CHECK(c.ready() == 3);
    for (uint64_t i = 0; i < 3; i++) {
        IqBlock b = c.pop();
        check_well_formed_block(b, kChunk, i * kChunk);
        CHECK(real_of(b) == samples(i * kChunk, kChunk));
    }
    CHECK(c.ready() == 0);
}

}  // namespace

int main() {
    test_completes_one_block();
    test_append_crosses_block_boundary();
    test_append_spans_multiple_blocks();
    test_append_zeros_fills_real_region();
    test_mixed_zeros_then_samples_in_one_block();
    test_zero_length_calls_are_noop();
    test_pop_on_empty_throws();
    test_flush_shrinks_partial_block();
    test_flush_noop_when_nothing_building();
    test_start_indices_advance_in_order();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("iq_block_chunker_test: all tests passed");
    return 0;
}
