// Tests for BlockStitcher: each buffer's guard padding is filled from its neighbors, as if the stream were one
// array, but only across contiguous buffers. Also IqBlock's sharing, and its buffer's return to the pool.
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

#include <coro/sync/mpsc.h>

#include "dsp/iq_block.h"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

namespace {

constexpr IqPadding kPad{3, 4};

// A block of n samples in format F whose values are their stream indices (+1, so 0 means "padding left zero").
template<class F = iq::F32>
IqBuffer ramp(size_t n, uint64_t start) {
    IqBuffer b = IqBuffer::zeroed<F>(kPad, n, start);
    for (size_t i = 0; i < n; i++) b.iq<F>()[2 * i] = static_cast<typename F::Sample>(start + i + 1);
    return b;
}

// The whole buffer, lead through trail, as the stream values it holds (0 where the padding stayed zero).
template<class F = iq::F32, class Block>
std::vector<int> contents(const Block& b) {
    std::vector<int> v;
    for (size_t i = 0; i < b.total(); i++) v.push_back(static_cast<int>(b.template padded<F>()[2 * i]));
    return v;
}

using V = std::vector<int>;

void test_contiguous() {
    BlockStitcher st;
    CHECK(!st.push(ramp(5, 0)));
    auto a = st.push(ramp(5, 5));
    CHECK(a && a->start() == 0);
    CHECK(contents(*a) == (V{0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
    auto b = st.push(ramp(5, 10));
    CHECK(contents(*b) == (V{3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}));
    auto c = st.flush();
    CHECK(c && contents(*c) == (V{8, 9, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0}));
    CHECK(!st.flush());
}

// A block shorter than the lead: the next block's lead reaches back through it into the one before, and it
// fills only part of the previous block's trail.
void test_short_block() {
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
void test_gap() {
    BlockStitcher st;
    st.push(ramp(5, 0));
    auto a = st.push(ramp(5, 10));
    CHECK(contents(*a) == (V{0, 0, 0, 1, 2, 3, 4, 5, 0, 0, 0, 0}));
    auto b = st.flush();
    CHECK(contents(*b) == (V{0, 0, 0, 11, 12, 13, 14, 15, 0, 0, 0, 0}));
}

// Blocks are stitched in whatever format they hold, as raw stored values with no conversion.
void test_other_format() {
    BlockStitcher st;
    st.push(ramp<iq::I16Be>(5, 0));
    auto a = st.push(ramp<iq::I16Be>(5, 5));
    CHECK(a && a->holds<iq::I16Be>());
    CHECK(contents<iq::I16Be>(*a) == (V{0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
    auto b = st.flush();
    CHECK(contents<iq::I16Be>(*b) == (V{3, 4, 5, 6, 7, 8, 9, 10, 0, 0, 0, 0}));
}

// A change of format between contiguous blocks is treated like a gap.
void test_format_change() {
    BlockStitcher st;
    st.push(ramp<iq::F32>(5, 0));
    auto a = st.push(ramp<iq::I16Be>(5, 5));
    CHECK(contents<iq::F32>(*a) == (V{0, 0, 0, 1, 2, 3, 4, 5, 0, 0, 0, 0}));
    auto b = st.flush();
    CHECK(contents<iq::I16Be>(*b) == (V{0, 0, 0, 6, 7, 8, 9, 10, 0, 0, 0, 0}));
}

// to_float() gives every format's samples scaled to [-1, 1]; I16Be's are stored byte-swapped.
void test_to_float() {
    IqBuffer f = IqBuffer::zeroed<iq::F32>(kPad, 2, 0);
    f.iq<iq::F32>()[0] = 0.25f;
    f.iq<iq::F32>()[3] = -0.5f;
    IqBuffer w = IqBuffer::zeroed<iq::I16Be>(kPad, 2, 0);
    // 0.25 and -0.5 of full scale are 0x2000 and 0xC000, whose bytes swapped are 0x0020 and 0x00C0.
    w.iq<iq::I16Be>()[0] = std::endian::native == std::endian::little ? int16_t{0x0020} : int16_t{0x2000};
    w.iq<iq::I16Be>()[3] = std::endian::native == std::endian::little ? int16_t{0x00C0} : static_cast<int16_t>(0xC000);
    for (IqBuffer* buffer : {&f, &w}) {
        const IqBlock b(std::move(*buffer));
        std::vector<float> out(2 * b.total());
        b.to_float(0, out);
        std::vector<float> want(out.size(), 0.0f);
        want[2 * kPad.lead] = 0.25f;
        want[2 * kPad.lead + 3] = -0.5f;
        CHECK(out == want);
    }
}

// A block's buffer goes back to its pool once its last share is dropped. A clone is a copy the pool never gets.
void test_recycle() {
    auto [tx, rx] = coro::mpsc_channel<iq::SamplesVariant>(4);
    IqBuffer buffer = IqBuffer::uninitialized<iq::F32>(
        kPad, 5, 0, std::make_shared<coro::MpscSender<iq::SamplesVariant>>(std::move(tx)), {});
    std::ranges::fill(buffer.iq<iq::F32>(), 1.0f);
    std::optional<IqBlock> block(IqBlock(std::move(buffer)));
    std::optional<IqBlock> shared = block->share();
    std::optional<IqBlock> cloned = block->clone();
    CHECK(shared->iq<iq::F32>().data() == block->iq<iq::F32>().data());
    CHECK(cloned->iq<iq::F32>().data() != block->iq<iq::F32>().data());
    CHECK(contents(*cloned) == contents(*block) && cloned->start() == 0 && cloned->n() == 5);
    block.reset();
    cloned.reset();
    CHECK(!rx.try_recv());
    shared.reset();
    auto back = rx.try_recv();
    const size_t total = static_cast<size_t>(kPad.lead + 5 + kPad.trail);
    CHECK(back && std::get<iq::Samples<iq::F32>>(*back).data.shape(0) == total);
    CHECK(!rx.try_recv());
}

}  // namespace

int main() {
    test_contiguous();
    test_short_block();
    test_gap();
    test_other_format();
    test_format_change();
    test_to_float();
    test_recycle();
    std::puts("block_stitcher_test: all passed");
}
