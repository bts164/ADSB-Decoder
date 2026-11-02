#include "output/capture_summary.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <variant>

namespace {

static_assert(std::endian::native == std::endian::little, "the summary's fields are written as the host's");
static_assert(sizeof(SummaryBucket) == SummaryHeader::kBucketBytes, "a bucket is stored as its struct");

constexpr std::array<char, 8> kMagic = {'A', 'D', 'S', 'B', 'S', 'U', 'M', '\0'};

// The header's number for each format, which stays the same whatever the order of iq::RecordingFormats, so a
// summary stays readable.
template <class F>
constexpr uint32_t kFormatNumber = 0;
template <>
constexpr uint32_t kFormatNumber<iq::F32> = 1;
template <>
constexpr uint32_t kFormatNumber<iq::I16Be> = 2;
template <>
constexpr uint32_t kFormatNumber<iq::RF32> = 3;

// One sample, as I and Q (Q 0 for a real one).
struct Sample {
    double i;
    double q;
};

// The sample in format F at `p`.
template <class F>
Sample sample_at(const std::byte* p) {
    std::array<typename F::Sample, F::complex ? 2 : 1> c;
    std::memcpy(c.data(), p, sizeof c);
    if constexpr (F::complex) return {F::to_float(c[0]), F::to_float(c[1])};
    else return {F::to_float(c[0]), 0};
}

template <class T>
void put(std::vector<std::byte>& out, size_t offset, T value) {
    std::memcpy(out.data() + offset, &value, sizeof value);
}

template <class T>
T get(std::span<const std::byte> in, size_t offset) {
    T value;
    std::memcpy(&value, in.data() + offset, sizeof value);
    return value;
}

}  // namespace

float clip_level(const iq::RecordingFormat& format, std::optional<double> meta_full_scale) {
    return std::visit(
        [&]<class F>(F) -> float {
            if constexpr (F::integer) return std::numeric_limits<typename F::Sample>::max();
            else if constexpr (F::complex)
                return meta_full_scale && *meta_full_scale > 0 ? static_cast<float>(*meta_full_scale) : 0;
            else return 0;
        },
        format);
}

void BucketSum::add_sample(double i, double q, bool clipped) {
    const double power = i * i + q * q;
    m_max_power = std::max(m_max_power, power);
    m_power += power;
    m_i += i;
    m_q += q;
    m_clipped += clipped;
    m_samples++;
}

void BucketSum::add(const SummaryBucket& b, uint64_t samples) {
    const auto n = static_cast<double>(samples);
    m_max_power = std::max(m_max_power, static_cast<double>(b.max_power));
    m_power += b.mean_power * n;
    m_i += b.mean_i * n;
    m_q += b.mean_q * n;
    m_clipped += b.clipped;
    m_samples += samples;
}

SummaryBucket BucketSum::bucket() const {
    if (m_samples == 0) return {};
    const auto n = static_cast<double>(m_samples);
    return {.max_power = static_cast<float>(m_max_power),
            .mean_power = static_cast<float>(m_power / n),
            .mean_i = static_cast<float>(m_i / n),
            .mean_q = static_cast<float>(m_q / n),
            .clipped = static_cast<uint32_t>(std::min<uint64_t>(m_clipped, std::numeric_limits<uint32_t>::max()))};
}

SummaryBucket reduce_samples(const iq::RecordingFormat& format, std::span<const std::byte> raw, float clip) {
    return std::visit(
        [&]<class F>(F) {
            constexpr size_t step = iq::bytes_per_sample<F>();
            BucketSum sum;
            for (size_t at = 0; at + step <= raw.size(); at += step) {
                const Sample s = sample_at<F>(raw.data() + at);
                sum.add_sample(s.i, s.q, clip > 0 && (std::abs(s.i) >= clip || std::abs(s.q) >= clip));
            }
            return sum.bucket();
        },
        format);
}

std::vector<float> sample_values(const iq::RecordingFormat& format, std::span<const std::byte> raw, bool iq) {
    return std::visit(
        [&]<class F>(F) {
            constexpr size_t step = iq::bytes_per_sample<F>();
            const bool pairs = iq && F::complex;
            std::vector<float> out;
            out.reserve(raw.size() / step * (pairs ? 2 : 1));
            for (size_t at = 0; at + step <= raw.size(); at += step) {
                const Sample s = sample_at<F>(raw.data() + at);
                if (pairs) {
                    out.push_back(static_cast<float>(s.i));
                    out.push_back(static_cast<float>(s.q));
                } else if (F::complex) {
                    out.push_back(static_cast<float>(std::hypot(s.i, s.q)));
                } else {
                    out.push_back(static_cast<float>(s.i));
                }
            }
            return out;
        },
        format);
}

std::vector<uint64_t> level_buckets(uint64_t samples, uint32_t bucket1, uint32_t factor) {
    std::vector<uint64_t> counts;
    if (samples == 0 || bucket1 == 0 || factor < 2) return counts;
    for (uint64_t size = bucket1;; size *= factor) {
        const uint64_t n = samples / size + (samples % size != 0);
        counts.push_back(n);
        if (n <= 1 || size > std::numeric_limits<uint64_t>::max() / factor) break;
    }
    return counts;
}

std::vector<SummaryBucket> next_level(std::span<const SummaryBucket> below, uint64_t first, uint64_t below_samples,
                                      uint64_t total_samples, uint32_t factor) {
    std::vector<SummaryBucket> out;
    out.reserve(below.size() / factor + 1);
    for (size_t group = 0; group < below.size(); group += factor) {
        BucketSum sum;
        for (size_t j = group; j < std::min<size_t>(below.size(), group + factor); j++) {
            const uint64_t start = (first + j) * below_samples;
            sum.add(below[j], std::min(below_samples, total_samples - start));
        }
        out.push_back(sum.bucket());
    }
    return out;
}

SummaryHeader SummaryHeader::plan(uint64_t data_bytes, int64_t data_mtime_ns, uint64_t samples,
                                  const iq::RecordingFormat& format, float full_scale) {
    SummaryHeader h{.data_bytes = data_bytes,
                    .data_mtime_ns = data_mtime_ns,
                    .samples = samples,
                    .format = format,
                    .full_scale = full_scale,
                    .levels = {}};
    const std::vector<uint64_t> counts = level_buckets(samples, h.bucket1, h.factor);
    uint64_t offset = kFixedBytes + kLevelEntryBytes * counts.size();
    for (uint64_t n : counts) {
        h.levels.push_back({offset, n});
        offset += n * kBucketBytes;
    }
    return h;
}

uint64_t SummaryHeader::bucket_samples(size_t level) const {
    uint64_t size = bucket1;
    for (size_t k = 1; k < level; k++) size *= factor;
    return size;
}

uint64_t SummaryHeader::file_bytes() const {
    if (levels.empty()) return bytes();
    return levels.back().offset + levels.back().buckets * kBucketBytes;
}

std::vector<std::byte> SummaryHeader::encode() const {
    std::vector<std::byte> out(bytes());
    std::memcpy(out.data(), kMagic.data(), kMagic.size());
    put(out, 8, kVersion);
    put(out, 12, kBucketBytes);
    put(out, 16, data_bytes);
    put(out, 24, data_mtime_ns);
    put(out, 32, samples);
    put(out, 40, std::visit(
                     []<class F>(F) {
                         static_assert(kFormatNumber<F> != 0, "every recording format needs a header number");
                         return kFormatNumber<F>;
                     },
                     format));
    put(out, 44, bucket1);
    put(out, 48, factor);
    put(out, 52, static_cast<uint32_t>(levels.size()));
    put(out, 56, full_scale);
    put(out, 60, uint32_t{0});
    for (size_t k = 0; k < levels.size(); k++) {
        put(out, kFixedBytes + kLevelEntryBytes * k, levels[k].offset);
        put(out, kFixedBytes + kLevelEntryBytes * k + 8, levels[k].buckets);
    }
    return out;
}

std::expected<SummaryHeader, std::string> SummaryHeader::decode(std::span<const std::byte> head,
                                                                uint64_t file_bytes) {
    if (head.size() < kFixedBytes) return std::unexpected("shorter than its header");
    if (std::memcmp(head.data(), kMagic.data(), kMagic.size()) != 0) return std::unexpected("not a summary");
    if (const auto version = get<uint32_t>(head, 8); version != kVersion)
        return std::unexpected("version " + std::to_string(version) + ", not " + std::to_string(kVersion));
    if (get<uint32_t>(head, 12) != kBucketBytes) return std::unexpected("buckets of another size");
    SummaryHeader h;
    h.data_bytes = get<uint64_t>(head, 16);
    h.data_mtime_ns = get<int64_t>(head, 24);
    h.samples = get<uint64_t>(head, 32);
    const auto number = get<uint32_t>(head, 40);
    const std::optional<iq::RecordingFormat> format =
        iq::RecordingFormats::find([&]<class F>() { return kFormatNumber<F> == number; });
    if (!format) return std::unexpected("an unknown datatype");
    h.format = *format;
    h.bucket1 = get<uint32_t>(head, 44);
    h.factor = get<uint32_t>(head, 48);
    const auto levels = get<uint32_t>(head, 52);
    h.full_scale = get<float>(head, 56);
    const std::vector<uint64_t> counts = level_buckets(h.samples, h.bucket1, h.factor);
    if (counts.empty() || levels != counts.size()) return std::unexpected("levels that don't fit its samples");
    if (head.size() < kFixedBytes + kLevelEntryBytes * levels) return std::unexpected("a cut-off level table");
    uint64_t offset = kFixedBytes + kLevelEntryBytes * levels;
    for (size_t k = 0; k < levels; k++) {
        const Level level{get<uint64_t>(head, kFixedBytes + kLevelEntryBytes * k),
                          get<uint64_t>(head, kFixedBytes + kLevelEntryBytes * k + 8)};
        if (level.offset != offset || level.buckets != counts[k])
            return std::unexpected("level " + std::to_string(k + 1) + " out of place");
        h.levels.push_back(level);
        offset += level.buckets * kBucketBytes;
    }
    if (offset != file_bytes)
        return std::unexpected(std::to_string(file_bytes) + " bytes, not the " + std::to_string(offset) +
                               " its levels take");
    return h;
}

std::vector<std::byte> encode_buckets(std::span<const SummaryBucket> buckets) {
    const auto bytes = std::as_bytes(buckets);
    return {bytes.begin(), bytes.end()};
}

std::vector<SummaryBucket> decode_buckets(std::span<const std::byte> bytes) {
    std::vector<SummaryBucket> out(bytes.size() / sizeof(SummaryBucket));
    std::memcpy(out.data(), bytes.data(), out.size() * sizeof(SummaryBucket));
    return out;
}

uint64_t column_start(uint64_t first, uint64_t count, uint32_t columns, uint32_t c) {
    // floor(c * count / columns), without the product overflowing.
    return first + c * (count / columns) + c * (count % columns) / columns;
}

size_t view_level(const SummaryHeader& header, double samples_per_column) {
    size_t level = 0;
    for (size_t k = 1; k <= header.levels.size(); k++)
        if (static_cast<double>(header.bucket_samples(k)) <= samples_per_column) level = k;
    return level;
}

ViewColumns columns_from_buckets(std::span<const SummaryBucket> buckets, uint64_t first_bucket,
                                 uint64_t bucket_samples, uint64_t total_samples, uint64_t first, uint64_t count,
                                 uint32_t columns) {
    std::vector<BucketSum> sums(columns);
    uint32_t c = 0;
    for (size_t j = 0; j < buckets.size(); j++) {
        const uint64_t start = (first_bucket + j) * bucket_samples;
        const uint64_t in_span = std::max(start, first);
        while (c + 1 < columns && column_start(first, count, columns, c + 1) <= in_span) c++;
        sums[c].add(buckets[j], std::min(bucket_samples, total_samples - start));
    }
    ViewColumns out;
    for (const BucketSum& sum : sums) {
        const SummaryBucket b = sum.bucket();
        out.max_power.push_back(b.max_power);
        out.mean_power.push_back(b.mean_power);
        out.clipped.push_back(b.clipped);
    }
    return out;
}

ViewColumns columns_from_samples(const iq::RecordingFormat& format, std::span<const std::byte> raw, float clip,
                                 uint32_t columns) {
    const size_t step = iq::bytes_per_sample(format);
    const uint64_t count = raw.size() / step;
    ViewColumns out;
    for (uint32_t c = 0; c < columns; c++) {
        const uint64_t from = column_start(0, count, columns, c);
        const uint64_t to = column_start(0, count, columns, c + 1);
        const SummaryBucket b = reduce_samples(format, raw.subspan(from * step, (to - from) * step), clip);
        out.max_power.push_back(b.max_power);
        out.mean_power.push_back(b.mean_power);
        out.clipped.push_back(b.clipped);
    }
    return out;
}
