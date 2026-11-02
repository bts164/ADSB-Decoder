#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dsp/iq_format.h"

/**
 * @file
 * A recording's summary: statistics of its samples per bucket, at several resolutions, from which the IQ viewer
 * (capture_viewer.h) reduces any span to a few thousand columns with one small read. This is the format's pure
 * part, with no I/O: the header's encoding, the statistics, and the reduction to columns.
 * doc/design/iq_viewer.md describes the file.
 */

/**
 * The I or Q magnitude counted as clipped in a recording in `format`: an integer format's limit, the metadata's
 * `adsb:full_scale` for a float IQ one, and 0, counting none, for real samples or when it's unknown.
 */
float clip_level(const iq::RecordingFormat& format, std::optional<double> meta_full_scale);

/** One bucket's statistics, as the summary stores them, in the samples' own units. */
struct SummaryBucket {
    float max_power = 0;   // the most of |x|^2
    float mean_power = 0;  // the mean of |x|^2
    float mean_i = 0;
    float mean_q = 0;      // 0 for real samples
    uint32_t clipped = 0;  // samples with I or Q at the clip level or over it, saturating
};

/** Accumulates samples, or whole buckets, into one bucket, in double, so a long bucket's means keep their precision. */
class BucketSum {
public:
    void add_sample(double i, double q, bool clipped);
    /** Adds a bucket of `samples` samples, weighting its means by them. */
    void add(const SummaryBucket& b, uint64_t samples);
    SummaryBucket bucket() const;
    uint64_t samples() const { return m_samples; }

private:
    double m_max_power = 0;
    double m_power = 0;
    double m_i = 0;
    double m_q = 0;
    uint64_t m_clipped = 0;
    uint64_t m_samples = 0;
};

/**
 * The statistics of the samples in `raw`, whole samples in `format`.
 * @param clip see clip_level(); 0 counts none clipped
 */
SummaryBucket reduce_samples(const iq::RecordingFormat& format, std::span<const std::byte> raw, float clip);

/** `raw`'s samples as floats: for a complex format, each one's magnitude, or with `iq` its I and Q interleaved. */
std::vector<float> sample_values(const iq::RecordingFormat& format, std::span<const std::byte> raw, bool iq);

/** The buckets in each level of a summary of `samples`, level 1 first: levels until one bucket covers them all. */
std::vector<uint64_t> level_buckets(uint64_t samples, uint32_t bucket1, uint32_t factor);

/**
 * The next level's buckets from `below`'s, `factor` to a bucket, the last taking what's left.
 * @param first the level index of below[0], a multiple of `factor`
 * @param below_samples the samples in each of below's buckets but the recording's last
 * @param total_samples the recording's samples, which give its last bucket's
 */
std::vector<SummaryBucket> next_level(std::span<const SummaryBucket> below, uint64_t first, uint64_t below_samples,
                                      uint64_t total_samples, uint32_t factor);

/** A summary's header and level table. */
struct SummaryHeader {
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kBucketBytes = 20;
    static constexpr size_t kFixedBytes = 64;
    static constexpr size_t kLevelEntryBytes = 16;
    /** Most levels a header can have: a bucket of 2 samples at a factor of 2 covers 2^64 samples in 64. */
    static constexpr size_t kMaxLevels = 64;
    /** The most bytes decode() needs, which a reader reads at once. */
    static constexpr size_t kMaxBytes = kFixedBytes + kLevelEntryBytes * kMaxLevels;
    /** `B1`: samples in a level-1 bucket. */
    static constexpr uint32_t kBucket1 = 256;
    /** `F`: a level's buckets in a bucket of the next. */
    static constexpr uint32_t kFactor = 16;

    struct Level {
        uint64_t offset;  // in the file
        uint64_t buckets;
    };

    uint64_t data_bytes = 0;    // the .sigmf-data's size when the summary was built
    int64_t data_mtime_ns = 0;  // its modification time then
    uint64_t samples = 0;
    iq::RecordingFormat format;
    uint32_t bucket1 = kBucket1;
    uint32_t factor = kFactor;
    float full_scale = 0;       // the clip level, clip_level()
    std::vector<Level> levels;  // level 1 first

    /** The header of a summary of `samples` (over 0), with its levels laid out one after another, after it. */
    static SummaryHeader plan(uint64_t data_bytes, int64_t data_mtime_ns, uint64_t samples,
                              const iq::RecordingFormat& format, float full_scale);

    /** The samples in a bucket of `level`, 1 to levels.size(). */
    uint64_t bucket_samples(size_t level) const;

    /** The header's bytes, with its level table. */
    size_t bytes() const { return kFixedBytes + kLevelEntryBytes * levels.size(); }

    /** The whole summary's bytes. */
    uint64_t file_bytes() const;

    std::vector<std::byte> encode() const;

    /**
     * Reads a header, checking it describes a summary of `file_bytes` bytes as plan() lays one out.
     * @param head the file's first bytes: kMaxBytes of them, or all of a shorter file
     * @return the header, or what's wrong with it
     */
    static std::expected<SummaryHeader, std::string> decode(std::span<const std::byte> head, uint64_t file_bytes);
};

/** Buckets as the summary stores them, and back. */
std::vector<std::byte> encode_buckets(std::span<const SummaryBucket> buckets);
std::vector<SummaryBucket> decode_buckets(std::span<const std::byte> bytes);

/** The first sample of column `c` of a span of `count` samples from `first` in `columns` columns. */
uint64_t column_start(uint64_t first, uint64_t count, uint32_t columns, uint32_t c);

/**
 * The level a view at `samples_per_column` reads: the coarsest whose bucket is no larger, or 0, for the samples,
 * when even level 1's is.
 */
size_t view_level(const SummaryHeader& header, double samples_per_column);

/** A view's columns. */
struct ViewColumns {
    std::vector<float> max_power;
    std::vector<float> mean_power;
    std::vector<uint32_t> clipped;
};

/**
 * A span's columns from one level's buckets: each bucket goes whole to the column its first sample in the span is
 * in. Needs a bucket no larger than a column, so that every column gets one.
 * @param buckets those overlapping the span, the first at level index `first_bucket`
 * @param bucket_samples the samples in each but the recording's last
 */
ViewColumns columns_from_buckets(std::span<const SummaryBucket> buckets, uint64_t first_bucket,
                                 uint64_t bucket_samples, uint64_t total_samples, uint64_t first, uint64_t count,
                                 uint32_t columns);

/**
 * A span's columns from its samples, exactly. Needs at least as many samples as columns.
 * @param raw the span's samples, in `format`
 */
ViewColumns columns_from_samples(const iq::RecordingFormat& format, std::span<const std::byte> raw, float clip,
                                 uint32_t columns);
