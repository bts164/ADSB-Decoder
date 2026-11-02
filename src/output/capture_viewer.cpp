#include "output/capture_viewer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <map>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include <coro/io/file.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/select.h>
#include <coro/sync/sleep.h>
#include <coro/task/join_handle.h>
#include <coro/task/parallel.h>
#include <coro/task/spawn_blocking.h>
#include <coro/task/spawn_builder.h>

#include "common/log.h"
#include "common/trace.h"
#include "output/capture.h"

namespace {

using wsrpc::CallError;
using Error = wsrpc::v1::Error;
using BuildProgress = CaptureViewer::BuildProgress;

// Time between an open's progress reports.
constexpr auto kReportInterval = std::chrono::milliseconds(500);

// Opens waiting for the viewer task.
constexpr size_t kQueueRequests = 16;

// Level-1 buckets a build reads at once: 8 MB of cf32. A multiple of kFactor, so each read's buckets make whole
// level-2 buckets but the recording's last.
constexpr uint64_t kChunkBuckets = 4096;
static_assert(kChunkBuckets % SummaryHeader::kFactor == 0);

// A recording in the capture directory, as its files are now.
struct Recording {
    std::string name;
    std::string data_path;
    std::string summary_path;
    coro::File data;
    uint64_t data_bytes;
    int64_t data_mtime_ns;
    api::CaptureMeta meta;
    iq::RecordingFormat format;
    uint64_t samples;
    float clip;
};

// Finds `name` in `dir`, with metadata the viewer reads.
coro::Coro<Recording> recording(const std::string& dir, const std::string& name) {
    if (!is_capture_name(name))
        throw CallError(Error::INVALID_ARGUMENT, "\"" + name + "\" isn't a recording's name");
    const std::string base = (std::filesystem::path(dir) / name).string();
    const std::string data_path = base + ".sigmf-data";
    std::optional<coro::File> data;
    std::string failed;
    bool missing = false;
    try {
        data.emplace(co_await coro::File::open(data_path, coro::FileMode::Read));
    } catch (const std::system_error& e) {
        missing = e.code() == std::errc::no_such_file_or_directory;
        failed = e.what();
    } catch (const std::exception& e) {
        failed = e.what();
    }
    if (missing) throw CallError(Error::FAILED_PRECONDITION, "no such recording: " + data_path);
    if (!data) {
        LOGF(ERROR, "capture viewer: can't open %s: %s", data_path, failed);
        throw CallError(Error::INTERNAL, data_path + ": " + failed);
    }
    const coro::FileMetadata file = co_await data->metadata();
    if (!file.is_regular) throw CallError(Error::FAILED_PRECONDITION, data_path + " isn't a file");
    auto meta = co_await coro::spawn_blocking(
        [path = base + ".sigmf-meta", size = file.size] { return read_capture_meta(path, size); });
    if (!meta) throw CallError(Error::FAILED_PRECONDITION, base + ".sigmf-meta is missing or invalid");
    const std::optional<iq::RecordingFormat> format = iq::RecordingFormats::from_sigmf(meta->datatype());
    if (!format)
        throw CallError(Error::FAILED_PRECONDITION, data_path + " is " + meta->datatype() +
                                                        ", which the viewer doesn't read");
    const std::optional<double> full_scale = meta->has_full_scale() ? std::optional(meta->full_scale()) : std::nullopt;
    co_return Recording{.name = name,
                        .data_path = data_path,
                        .summary_path = base + ".adsb-summary",
                        .data = std::move(*data),
                        .data_bytes = file.size,
                        .data_mtime_ns = file.modified.time_since_epoch().count(),
                        .meta = std::move(*meta),
                        .format = *format,
                        .samples = file.size / iq::bytes_per_sample(*format),
                        .clip = clip_level(*format, full_scale)};
}

// A recording's summary as it is on disk, or why it can't be read.
struct Summary {
    std::optional<coro::File> file;
    SummaryHeader header;
    std::string why;  // without a file: why there's no current summary
};

coro::Coro<Summary> read_summary(const std::string& path) {
    std::optional<coro::File> file;
    try {
        file.emplace(co_await coro::File::open(path, coro::FileMode::Read));
    } catch (const std::system_error& e) {
        if (e.code() != std::errc::no_such_file_or_directory) throw;
    }
    if (!file) co_return Summary{.file = std::nullopt, .header = {}, .why = "there's none"};
    const uint64_t size = (co_await file->metadata()).size;
    auto [n, head] = co_await file->read_at_exact(
        std::vector<std::byte>(std::min<uint64_t>(size, SummaryHeader::kMaxBytes)), 0);
    head.resize(n);
    auto header = SummaryHeader::decode(head, size);
    if (!header) co_return Summary{.file = std::nullopt, .header = {}, .why = "it's invalid: " + header.error()};
    co_return Summary{.file = std::move(file), .header = std::move(*header), .why = {}};
}

// Why `header` doesn't summarise `r` as it is now, or nullopt when it does.
std::optional<std::string> stale(const SummaryHeader& header, const Recording& r) {
    if (header.data_bytes != r.data_bytes) return "the recording's size changed";
    if (header.data_mtime_ns != r.data_mtime_ns) return "the recording changed";
    if (header.samples != r.samples || header.format != r.format) return "it describes other samples";
    if (header.full_scale != r.clip) return "the recording's full scale changed";
    if (header.bucket1 != SummaryHeader::kBucket1 || header.factor != SummaryHeader::kFactor)
        return "its buckets aren't this version's";
    return std::nullopt;
}

// `r`'s samples from `first`.
coro::Coro<std::vector<std::byte>> read_samples(Recording& r, uint64_t first, uint64_t count) {
    const size_t bps = iq::bytes_per_sample(r.format);
    auto [got, raw] =
        co_await r.data.read_at_exact(std::vector<std::byte>(count * bps), static_cast<int64_t>(first * bps));
    if (got < raw.size()) throw CallError(Error::INTERNAL, r.data_path + " is shorter than its size");
    co_return std::move(raw);
}

// Waits for a build to finish, or its task to end without finishing.
coro::Coro<void> until_finished(coro::WatchReceiver<BuildProgress>& progress) {
    while (!progress.borrow_and_update()->finished)
        if (!co_await progress.changed()) co_return;
}

// Writes `r`'s summary beside it, as `<name>.adsb-summary.tmp` renamed when it's whole, so a reader never sees
// part of one.
coro::Coro<void> write_summary(Recording& r, coro::WatchSender<BuildProgress>& progress) {
    const std::string tmp_path = r.summary_path + ".tmp";
    const SummaryHeader header = SummaryHeader::plan(r.data_bytes, r.data_mtime_ns, r.samples, r.format, r.clip);
    // A .tmp left by a build that was cancelled, or failed, is nobody's.
    co_await coro::spawn_blocking([tmp_path] {
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
    });
    std::optional<coro::File> out;
    std::string failed;
    try {
        out.emplace(co_await coro::File::open(tmp_path, coro::FileMode::Write | coro::FileMode::CreateNew));
    } catch (const std::exception& e) {
        failed = e.what();
    }
    if (!out) {
        LOGF(WARNING, "capture viewer: can't write a summary of %s: %s", r.data_path, failed);
        throw CallError(Error::FAILED_PRECONDITION, "can't write a summary beside " + r.data_path + ": " + failed);
    }
    (void)co_await out->write_exact(header.encode());

    const size_t bps = iq::bytes_per_sample(r.format);
    const uint64_t bucket1 = header.bucket_samples(1);
    const uint64_t buckets1 = header.levels[0].buckets;
    std::vector<SummaryBucket> level2;  // all of it, from each read's level-1 buckets
    std::vector<std::byte> raw;
    for (uint64_t fb = 0; fb < buckets1; fb += kChunkBuckets) {
        const uint64_t nb = std::min(kChunkBuckets, buckets1 - fb);
        const uint64_t first = fb * bucket1;
        const uint64_t n = std::min(nb * bucket1, r.samples - first);
        raw.resize(n * bps);
        auto [got, buf] = co_await r.data.read_at_exact(std::move(raw), static_cast<int64_t>(first * bps));
        raw = std::move(buf);
        if (got < raw.size())
            throw CallError(Error::INTERNAL, r.data_path + " ended at " + std::to_string(first * bps + got) +
                                                 " bytes while it was summarised");
        std::vector<SummaryBucket> level1(nb);
        const size_t k = std::clamp<size_t>(coro::worker_count(), 1, nb);
        co_await coro::parallel(k, [&](coro::TeamMember part) -> coro::Coro<void> {
            TRACE_ZONE("summary_chunk");
            for (uint64_t j = nb * part.rank() / part.size(); j < nb * (part.rank() + 1) / part.size(); j++) {
                const uint64_t from = j * bucket1;
                const uint64_t len = std::min(bucket1, n - from);
                level1[j] = reduce_samples(r.format, std::span(raw).subspan(from * bps, len * bps), r.clip);
            }
            co_return;
        });
        (void)co_await out->write_at_exact(
            encode_buckets(level1),
            static_cast<int64_t>(header.levels[0].offset + fb * SummaryHeader::kBucketBytes));
        if (header.levels.size() > 1) {
            const auto next = next_level(level1, fb, bucket1, r.samples, header.factor);
            level2.insert(level2.end(), next.begin(), next.end());
        }
        (void)progress.send(BuildProgress{.done = first + n, .total = r.samples, .finished = false, .failed = {}});
    }
    std::vector<SummaryBucket> below = std::move(level2);
    for (size_t level = 2; level <= header.levels.size(); level++) {
        if (level > 2) below = next_level(below, 0, header.bucket_samples(level - 1), r.samples, header.factor);
        (void)co_await out->write_at_exact(encode_buckets(below),
                                           static_cast<int64_t>(header.levels[level - 1].offset));
    }
    co_await out->sync_all();
    out.reset();
    const auto renamed = co_await coro::spawn_blocking([tmp_path, path = r.summary_path] {
        std::error_code ec;
        std::filesystem::rename(tmp_path, path, ec);
        return ec;
    });
    if (renamed) throw CallError(Error::INTERNAL, "can't rename " + tmp_path + ": " + renamed.message());
}

// A build's task: makes sure `name` in `dir` has a current summary, updating `progress`.
coro::Coro<void> build(std::string dir, std::string name, coro::WatchSender<BuildProgress> progress) {
    std::optional<CallError> failed;
    try {
        Recording r = co_await recording(dir, name);
        const Summary summary = co_await read_summary(r.summary_path);
        const std::optional<std::string> why = summary.file ? stale(summary.header, r) : summary.why;
        if (why) {
            LOGF(INFO, "capture viewer: building the summary of %s: %s", r.data_path, *why);
            const auto start = std::chrono::steady_clock::now();
            co_await write_summary(r, progress);
            const SummaryHeader header =
                SummaryHeader::plan(r.data_bytes, r.data_mtime_ns, r.samples, r.format, r.clip);
            LOGF(INFO, "capture viewer: built the summary of %s in %.1f s: %u levels, %u bytes", r.data_path,
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
                 header.levels.size(), header.file_bytes());
        }
        (void)progress.send(BuildProgress{.done = r.samples, .total = r.samples, .finished = true, .failed = {}});
        co_return;
    } catch (const CallError& e) {
        if (e.code() == Error::INTERNAL) LOGF(ERROR, "capture viewer: summarising %s failed: %s", name, e.what());
        failed = e;
    } catch (const std::exception& e) {
        LOGF(ERROR, "capture viewer: summarising %s failed: %s", name, e.what());
        failed = CallError(Error::INTERNAL, e.what());
    }
    (void)progress.send(BuildProgress{.done = 0, .total = 0, .finished = true, .failed = std::move(failed)});
}

// A build the viewer task owns, and its progress, which keeps the watch open.
struct Building {
    coro::WatchReceiver<BuildProgress> progress;
    coro::JoinHandle<void> task;
};

}  // namespace

CaptureViewer CaptureViewer::spawn() {
    auto [tx, rx] = coro::mpsc_channel<Build>(kQueueRequests);
    coro::build_task().name("capture_viewer").spawn(run(std::move(rx))).detach();
    return CaptureViewer(std::move(tx));
}

coro::Coro<void> CaptureViewer::run(coro::MpscReceiver<Build> rx) {
    std::map<std::string, Building> builds;  // by data path
    while (auto request = co_await rx.recv()) {
        std::erase_if(builds, [](const auto& b) { return b.second.progress.get().finished; });
        Build& b = *request;
        if (b.reply.is_closed()) continue;
        auto it = builds.find(b.data_path);
        if (it == builds.end()) {
            auto [tx, progress] = coro::watch_channel(BuildProgress{});
            auto task = coro::build_task().name("summary_build").spawn(build(b.dir, b.name, std::move(tx)));
            it = builds.emplace(b.data_path, Building{std::move(progress), std::move(task)}).first;
        }
        (void)b.reply.send(it->second.progress.clone());
    }
}

coro::CoroStream<api::OpenCaptureProgress> CaptureViewer::open(std::string dir, api::OpenCaptureRequest request,
                                                               WritingCaptures writing) {
    const Recording r = co_await recording(dir, request.name());
    if (writing.contains(r.data_path))
        throw CallError(Error::FAILED_PRECONDITION, r.data_path + " is still being recorded");
    if (r.samples == 0) throw CallError(Error::FAILED_PRECONDITION, r.data_path + " has no samples");
    LOGF(INFO, "capture viewer: opened %s, %u samples of %s at %g Hz", r.data_path, r.samples, r.meta.datatype(),
         r.meta.rate_hz());
    const std::string meta_path = std::filesystem::path(r.data_path).replace_extension(".sigmf-meta").string();
    std::optional<CaptureAnnotations> annotations = co_await coro::spawn_blocking(
        [meta_path, samples = r.samples] { return read_capture_annotations(meta_path, samples); });
    if (!annotations)
        LOGF(WARNING, "capture viewer: %s went or became invalid since it was read; no annotations shown", meta_path);
    else if (annotations->unreadable > 0)
        LOGF(WARNING, "capture viewer: %u of the annotations in %s are invalid or past the recording's end; not shown",
             annotations->unreadable, meta_path);

    auto [reply, result] = coro::oneshot_channel<coro::WatchReceiver<BuildProgress>>();
    if (!co_await m_tx.send(Build{.dir = dir, .name = r.name, .data_path = r.data_path, .reply = std::move(reply)}))
        throw CallError(Error::INTERNAL, "the capture viewer has stopped");
    auto answer = co_await result.recv();
    if (!answer) throw CallError(Error::INTERNAL, "the capture viewer has stopped");
    coro::WatchReceiver<BuildProgress> progress = std::move(*answer);

    for (;;) {
        const auto woke = co_await coro::select(until_finished(progress), coro::sleep_for(kReportInterval));
        const BuildProgress p = progress.get();
        if (p.failed) throw *p.failed;
        if (p.finished) {
            api::OpenCaptureProgress last;
            last.set_samples_done(r.samples);
            last.set_samples_total(r.samples);
            api::CaptureInfo& info = *last.mutable_capture();
            info.set_name(r.name);
            info.set_samples(r.samples);
            info.set_rate_hz(r.meta.rate_hz());
            info.set_datatype(std::string(iq::sigmf_name(r.format)));
            info.set_complex(iq::is_complex(r.format));
            if (r.clip > 0) info.set_full_scale(r.clip);
            if (annotations)
                for (api::CaptureAnnotation& a : annotations->annotations) *info.add_annotations() = std::move(a);
            co_yield std::move(last);
            co_return;
        }
        if (woke.index() == 0) throw CallError(Error::INTERNAL, "the summary of " + r.data_path + " was abandoned");
        api::OpenCaptureProgress report;
        report.set_samples_done(p.done);
        report.set_samples_total(r.samples);
        co_yield std::move(report);
    }
}

coro::Coro<api::CaptureView> capture_view(std::string dir, api::GetCaptureViewRequest request) {
    if (request.columns() < 1 || request.columns() > CaptureViewer::kMaxColumns)
        throw CallError(Error::INVALID_ARGUMENT, "columns must be 1 to " + std::to_string(CaptureViewer::kMaxColumns));
    if (request.count() == 0) throw CallError(Error::INVALID_ARGUMENT, "count must be over 0");
    Recording r = co_await recording(dir, request.name());
    if (request.first() >= r.samples || request.count() > r.samples - request.first())
        throw CallError(Error::INVALID_ARGUMENT, "the span is past the end of " + r.data_path + ", " +
                                                     std::to_string(r.samples) + " samples");
    const uint64_t first = request.first();
    const uint64_t count = request.count();
    const uint32_t columns = request.columns();
    const double spc = static_cast<double>(count) / columns;

    api::CaptureView view;
    view.set_first(first);
    view.set_count(count);
    if (spc <= CaptureViewer::kSampleViewMax) {
        const bool iq = request.iq() && iq::is_complex(r.format);
        const std::vector<float> values = sample_values(r.format, co_await read_samples(r, first, count), iq);
        api::CaptureSamples& samples = *view.mutable_samples();
        samples.mutable_values()->Add(values.begin(), values.end());
        samples.set_iq(iq);
        VLOGF(1, "capture view: %s, %u samples from %u, as samples", r.name, count, first);
        co_return view;
    }
    ViewColumns out;
    size_t level = 0;
    if (spc < SummaryHeader::kBucket1) {
        out = columns_from_samples(r.format, co_await read_samples(r, first, count), r.clip, columns);
    } else {
        Summary summary = co_await read_summary(r.summary_path);
        std::optional<std::string> why = summary.file ? stale(summary.header, r) : summary.why;
        if (why) throw CallError(Error::FAILED_PRECONDITION, "no current summary: open it first (" + *why + ")");
        const SummaryHeader& header = summary.header;
        level = view_level(header, spc);
        const uint64_t bucket = header.bucket_samples(level);
        const uint64_t fb = first / bucket;
        const uint64_t lb = (first + count - 1) / bucket;
        const size_t bytes = (lb - fb + 1) * SummaryHeader::kBucketBytes;
        auto [got, raw] = co_await summary.file->read_at_exact(
            std::vector<std::byte>(bytes),
            static_cast<int64_t>(header.levels[level - 1].offset + fb * SummaryHeader::kBucketBytes));
        if (got < bytes) throw CallError(Error::INTERNAL, r.summary_path + " is shorter than its header says");
        out = columns_from_buckets(decode_buckets(raw), fb, bucket, r.samples, first, count, columns);
    }
    api::CaptureColumns& c = *view.mutable_columns();
    c.mutable_max_power()->Add(out.max_power.begin(), out.max_power.end());
    c.mutable_mean_power()->Add(out.mean_power.begin(), out.mean_power.end());
    c.mutable_clipped()->Add(out.clipped.begin(), out.clipped.end());
    c.set_samples_per_column(spc);
    c.set_level(static_cast<uint32_t>(level));
    VLOGF(1, "capture view: %s, %u samples from %u in %u columns, level %u", r.name, count, first, columns, level);
    co_return view;
}
