// Tests for capture_summary.h: the summary's levels, header and buckets, and the views' reduction to columns,
// each against a brute-force reduction of the same samples.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "output/capture_summary.h"

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                                 \
        }                                                                                 \
    } while (0)

namespace {

using Raw = std::vector<std::byte>;

constexpr uint32_t kB1 = SummaryHeader::kBucket1;
constexpr uint32_t kF = SummaryHeader::kFactor;

bool near(double a, double b) { return std::abs(a - b) <= 1e-4 * std::max({1.0, std::abs(a), std::abs(b)}); }

// `n` cf32 samples, uniform in [-1, 1), with every 97th at +1 on I, a clip at a full scale of 1.
Raw cf32_samples(uint64_t n) {
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> u(-1, 1);
    std::vector<float> iq(2 * n);
    for (uint64_t k = 0; k < n; k++) {
        iq[2 * k] = k % 97 == 0 ? 1.0f : u(rng);
        iq[2 * k + 1] = u(rng);
    }
    Raw raw(iq.size() * sizeof(float));
    std::memcpy(raw.data(), iq.data(), raw.size());
    return raw;
}

Raw ci16_be(std::initializer_list<int16_t> values) {
    Raw raw;
    for (int16_t v : values) {
        const auto u = static_cast<uint16_t>(v);
        raw.push_back(static_cast<std::byte>(u >> 8));
        raw.push_back(static_cast<std::byte>(u & 0xff));
    }
    return raw;
}

// The samples [from, to) of `raw`, reduced one by one.
SummaryBucket brute(const iq::RecordingFormat& format, const Raw& raw, uint64_t from, uint64_t to, float clip) {
    const size_t bps = iq::bytes_per_sample(format);
    return reduce_samples(format, std::span(raw).subspan(from * bps, (to - from) * bps), clip);
}

bool same(const SummaryBucket& a, const SummaryBucket& b) {
    return near(a.max_power, b.max_power) && near(a.mean_power, b.mean_power) && near(a.mean_i, b.mean_i) &&
           near(a.mean_q, b.mean_q) && a.clipped == b.clipped;
}

// Every level of a summary of `raw`, as the viewer builds it: level 1 in reads of `chunk` buckets, each read's
// level-2 buckets from its own, and the levels above from the whole level below.
std::vector<std::vector<SummaryBucket>> build_levels(const iq::RecordingFormat& format, const Raw& raw, float clip,
                                                     uint64_t chunk) {
    const uint64_t samples = raw.size() / iq::bytes_per_sample(format);
    const SummaryHeader h = SummaryHeader::plan(raw.size(), 0, samples, format, clip);
    std::vector<std::vector<SummaryBucket>> levels(h.levels.size());
    for (uint64_t fb = 0; fb < h.levels[0].buckets; fb += chunk) {
        std::vector<SummaryBucket> part;
        for (uint64_t j = fb; j < std::min(fb + chunk, h.levels[0].buckets); j++)
            part.push_back(brute(format, raw, j * kB1, std::min<uint64_t>((j + 1) * kB1, samples), clip));
        levels[0].insert(levels[0].end(), part.begin(), part.end());
        if (levels.size() > 1) {
            const auto next = next_level(part, fb, kB1, samples, kF);
            levels[1].insert(levels[1].end(), next.begin(), next.end());
        }
    }
    for (size_t k = 2; k < levels.size(); k++)
        levels[k] = next_level(levels[k - 1], 0, h.bucket_samples(k), samples, kF);
    return levels;
}

void test_samples() {
    CHECK(iq::RecordingFormats::from_sigmf("cf32_le") == iq::RecordingFormat(iq::F32{}));
    CHECK(iq::RecordingFormats::from_sigmf("ci16_be") == iq::RecordingFormat(iq::I16Be{}));
    CHECK(iq::RecordingFormats::from_sigmf("rf32_le") == iq::RecordingFormat(iq::RF32{}));
    CHECK(!iq::RecordingFormats::from_sigmf("ci16_le"));
    CHECK(iq::sigmf_name(iq::RecordingFormat(iq::I16Be{})) == "ci16_be");

    const Raw ci16 = ci16_be({3, -4, -32768, 1, 100, 32767});
    CHECK(sample_values(iq::I16Be{}, ci16, true) == (std::vector<float>{3, -4, -32768, 1, 100, 32767}));
    const std::vector<float> mags = sample_values(iq::I16Be{}, ci16, false);
    CHECK(mags.size() == 3 && near(mags[0], 5));
    // The integers' limit clips on either side: -32768 and 32767.
    CHECK(reduce_samples(iq::I16Be{}, ci16, clip_level(iq::I16Be{}, std::nullopt)).clipped == 2);
    const SummaryBucket b = reduce_samples(iq::I16Be{}, std::span(ci16).first(4), 32767);
    CHECK(b.max_power == 25 && b.mean_power == 25 && b.mean_i == 3 && b.mean_q == -4 && b.clipped == 0);

    // A real sample is its own value, never clipped.
    const float reals[] = {-2, 0.5f};
    const auto rf32 = std::as_bytes(std::span(reals));
    CHECK(sample_values(iq::RF32{}, rf32, true) == (std::vector<float>{-2, 0.5f}));
    CHECK(clip_level(iq::RF32{}, 1.0) == 0);
    const SummaryBucket r = reduce_samples(iq::RF32{}, rf32, 0);
    CHECK(r.max_power == 4 && near(r.mean_power, 2.125) && r.mean_q == 0 && r.clipped == 0);

    // cf32 clips only at a known full scale.
    CHECK(clip_level(iq::F32{}, std::nullopt) == 0);
    CHECK(clip_level(iq::F32{}, 1.0) == 1);
    const Raw cf32 = cf32_samples(97 * 3);
    CHECK(reduce_samples(iq::F32{}, cf32, 0).clipped == 0);
    CHECK(reduce_samples(iq::F32{}, cf32, 1).clipped == 3);
}

void test_level_buckets() {
    CHECK(level_buckets(0, kB1, kF).empty());
    CHECK(level_buckets(1, kB1, kF) == (std::vector<uint64_t>{1}));
    CHECK(level_buckets(kB1, kB1, kF) == (std::vector<uint64_t>{1}));
    CHECK(level_buckets(kB1 + 1, kB1, kF) == (std::vector<uint64_t>{2, 1}));
    CHECK(level_buckets(kB1 * kF, kB1, kF) == (std::vector<uint64_t>{16, 1}));
    CHECK(level_buckets(kB1 * kF + 1, kB1, kF) == (std::vector<uint64_t>{17, 2, 1}));
    CHECK(level_buckets(UINT64_MAX, 2, 2).size() <= SummaryHeader::kMaxLevels);
}

// Each bucket of each level is its samples' reduction, whatever the level-1 reads' size, through to a last
// bucket of only a few samples.
void test_levels_match_samples() {
    const uint64_t samples = kB1 * kF * kF + 300;
    const Raw raw = cf32_samples(samples);
    const SummaryHeader h = SummaryHeader::plan(raw.size(), 0, samples, iq::F32{}, 1);
    CHECK(h.levels.size() == 4);
    for (uint64_t chunk : {uint64_t{kF}, uint64_t{4096}}) {
        const auto levels = build_levels(iq::F32{}, raw, 1, chunk);
        for (size_t k = 0; k < levels.size(); k++) {
            const uint64_t size = h.bucket_samples(k + 1);
            CHECK(levels[k].size() == h.levels[k].buckets);
            for (uint64_t j = 0; j < levels[k].size(); j++)
                CHECK(same(levels[k][j],
                           brute(iq::F32{}, raw, j * size, std::min(samples, (j + 1) * size), 1)));
        }
    }
}

void test_header() {
    const SummaryHeader h = SummaryHeader::plan(123456 * 8, -5, 123456, iq::F32{}, 0.5f);
    const Raw head = h.encode();
    CHECK(head.size() == h.bytes() && h.levels[0].offset == h.bytes());
    const auto back = SummaryHeader::decode(head, h.file_bytes());
    CHECK(back);
    CHECK(back->data_bytes == h.data_bytes && back->data_mtime_ns == -5 && back->samples == 123456);
    CHECK(std::holds_alternative<iq::F32>(back->format) && back->full_scale == 0.5f);
    CHECK(back->bucket1 == kB1 && back->factor == kF);
    CHECK(back->levels.size() == h.levels.size());
    for (size_t k = 0; k < h.levels.size(); k++)
        CHECK(back->levels[k].offset == h.levels[k].offset && back->levels[k].buckets == h.levels[k].buckets);

    // A summary cut off, or grown, is bad, as is anything that doesn't describe plan()'s layout.
    CHECK(!SummaryHeader::decode(head, h.file_bytes() - 1));
    CHECK(!SummaryHeader::decode(head, h.file_bytes() + 1));
    CHECK(!SummaryHeader::decode(std::span(head).first(head.size() - 1), h.file_bytes()));
    CHECK(!SummaryHeader::decode(std::span(head).first(40), h.file_bytes()));
    const auto bad = [&](size_t at, std::byte value) {
        Raw b = head;
        b[at] = value;
        return !SummaryHeader::decode(b, h.file_bytes());
    };
    CHECK(bad(0, std::byte{'X'}));                                         // magic
    CHECK(bad(8, std::byte{2}));                                           // version
    CHECK(bad(40, std::byte{9}));                                          // datatype
    CHECK(bad(52, std::byte{1}));                                          // level count
    CHECK(bad(SummaryHeader::kFixedBytes, std::byte{0}));                  // level 1's offset
    CHECK(bad(SummaryHeader::kFixedBytes + 8, std::byte{0}));              // level 1's buckets

    const std::vector<SummaryBucket> buckets = {{1, 2, 3, 4, 5}, {6, 7, 8, 9, 10}};
    const Raw bytes = encode_buckets(buckets);
    CHECK(bytes.size() == 2 * SummaryHeader::kBucketBytes);
    const auto decoded = decode_buckets(bytes);
    CHECK(decoded.size() == 2 && decoded[1].mean_power == 7 && decoded[1].clipped == 10);
}

void test_view_level() {
    const SummaryHeader h = SummaryHeader::plan(0, 0, uint64_t{1} << 30, iq::F32{}, 0);
    CHECK(view_level(h, kB1 - 0.5) == 0);
    CHECK(view_level(h, kB1) == 1);
    CHECK(view_level(h, kB1 * kF - 1) == 1);
    CHECK(view_level(h, kB1 * kF) == 2);
    CHECK(view_level(h, 1e30) == h.levels.size());
}

void test_column_start() {
    CHECK(column_start(10, 100, 7, 0) == 10);
    CHECK(column_start(10, 100, 7, 7) == 110);
    for (uint32_t c = 0; c <= 7; c++) CHECK(column_start(10, 100, 7, c) == 10 + c * 100 / 7);
    // No overflow where c * count would.
    CHECK(column_start(0, UINT64_MAX, 8192, 4096) == UINT64_MAX / 2);
}

void test_columns_from_samples() {
    const uint64_t count = 1000;
    const Raw raw = cf32_samples(count);
    const ViewColumns v = columns_from_samples(iq::F32{}, raw, 1, 7);
    CHECK(v.max_power.size() == 7 && v.mean_power.size() == 7 && v.clipped.size() == 7);
    for (uint32_t c = 0; c < 7; c++) {
        const SummaryBucket b =
            brute(iq::F32{}, raw, column_start(0, count, 7, c), column_start(0, count, 7, c + 1), 1);
        CHECK(near(v.max_power[c], b.max_power) && near(v.mean_power[c], b.mean_power) && v.clipped[c] == b.clipped);
    }
}

// Each bucket goes whole to the column its first sample in the span is in, so a column is the reduction of its
// buckets' samples, which may reach past the span at either end.
void test_columns_from_buckets() {
    const uint64_t samples = kB1 * kF * 5 + 77;
    const Raw raw = cf32_samples(samples);
    const auto levels = build_levels(iq::F32{}, raw, 1, 4096);
    const SummaryHeader h = SummaryHeader::plan(raw.size(), 0, samples, iq::F32{}, 1);
    struct Span {
        uint64_t first, count;
        uint32_t columns;
        size_t level;
    };
    for (const Span s : {Span{0, samples, 3, 2}, Span{100, samples - 100, 7, 1}, Span{1000, 5000, 9, 1},
                         Span{kB1 * 3 + 5, kB1 * 40, 40, 1}}) {
        const uint64_t size = h.bucket_samples(s.level);
        const uint64_t fb = s.first / size;
        const uint64_t lb = (s.first + s.count - 1) / size;
        const auto& level = levels[s.level - 1];
        const ViewColumns v = columns_from_buckets(std::span(level).subspan(fb, lb - fb + 1), fb, size, samples,
                                                   s.first, s.count, s.columns);
        CHECK(v.max_power.size() == s.columns);
        std::vector<uint64_t> from(s.columns, UINT64_MAX), to(s.columns, 0);
        for (uint64_t j = fb; j <= lb; j++) {
            const uint64_t at = std::max(j * size, s.first);
            uint32_t c = 0;
            while (c + 1 < s.columns && column_start(s.first, s.count, s.columns, c + 1) <= at) c++;
            from[c] = std::min(from[c], j * size);
            to[c] = std::max(to[c], std::min(samples, (j + 1) * size));
        }
        for (uint32_t c = 0; c < s.columns; c++) {
            CHECK(from[c] < to[c]);  // every column gets a bucket
            const SummaryBucket b = brute(iq::F32{}, raw, from[c], to[c], 1);
            CHECK(near(v.max_power[c], b.max_power) && near(v.mean_power[c], b.mean_power) &&
                  v.clipped[c] == b.clipped);
        }
    }
}

}  // namespace

int main() {
    test_samples();
    test_level_buckets();
    test_levels_match_samples();
    test_header();
    test_view_level();
    test_column_start();
    test_columns_from_samples();
    test_columns_from_buckets();
    std::puts("all passed");
}
