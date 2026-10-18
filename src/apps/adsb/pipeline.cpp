#include "apps/adsb/pipeline.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <vector>

#include <coro/coro_stream.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/mpsc.h>
#include <coro/task/join_handle.h>
#include <coro/task/spawn_builder.h>
#include <nlohmann/json.hpp>

#include "common/log.h"
#include "common/trace.h"
#include "decode/aircraft.h"
#include "dsp/spectrum.h"
#include "output/frame_recorder.h"
#include "output/ws_publisher.h"

namespace {

// Longest an aircraft goes unpublished while frames from it keep arriving.
constexpr double kAircraftRefreshS = 1.0;

// Interval between run_demod_loop's `ADSB_TIMING` throughput reports.
constexpr std::chrono::milliseconds kStatusReportInterval{1000};
// Least time between published spectrum frames: a waterfall only needs a few a second.
constexpr std::chrono::milliseconds kSpectrumInterval{150};

// Adds one JSON object to the text a block publishes (see ws_publisher_publish()).
void append_json(std::string& out, const std::string& json) {
    if (!out.empty()) out += ',';
    out += json;
}

}  // namespace

DecodedFrameView handle_frame(AircraftTracker& tracker, const AdsbFrame& f, double rate_hz, double start_unix_s,
                              std::string& out, std::vector<HistoryRow>* history_rows) {
    DecodedFrameView v = tracker.view(f, rate_hz);
    TRACE_ZONE("handle_frame");
    VLOGF(2, "frame %v", v);
    nlohmann::json frame = v;
    const std::string summary = frame_summary(v, v.icao_known ? tracker.find(v.icao) : nullptr);
    frame["summary"] = summary.empty() ? nlohmann::json(nullptr) : nlohmann::json(summary);
    append_json(out, frame.dump());

    // Every frame whose address checks out counts toward its aircraft's
    // messages and signal level, and marks it seen.
    if (v.crc_ok && v.icao_known) {
        double t_s = static_cast<double>(v.idx) / rate_hz;
        AircraftState& ac = tracker.aircraft(v.icao);
        const bool changed = update_aircraft_from_frame(ac, v.df, v.payload, t_s);
        update_aircraft_reception(ac, v.confidence, v.num_bits);
        ac.last_seen_s = t_s;
        ac.last_seen_unix_s = start_unix_s + t_s;
        // The counters and last-seen time change with every frame, so they
        // go out with the next real change or at most once a second.
        if (changed) VLOGF(1, "aircraft %v", ac);
        if (changed || t_s - ac.last_published_s >= kAircraftRefreshS) {
            ac.last_published_s = t_s;
            std::string json = nlohmann::json(ac).dump();
            append_json(out, json);
            if (history_rows) history_rows->push_back({ac.last_seen_unix_s, ac.icao, ac.lat, ac.lon, std::move(json)});
        }
    }
    return v;
}

void DemodLoopParams::add_options(argparse::ArgumentParser& program) {
    program.add_argument("--preamble-min").scan<'g', float>().default_value(3.0f).help(
        "preamble correlation score threshold");
    program.add_argument("--slice-mag-min").scan<'g', float>().default_value(2.0f * 56.0f).help(
        "bit-slice magnitude threshold");
    program.add_argument("--debug-h5").help(
        "write every decoded frame, with its IQ, envelope and the filter and parameters to rerun the demodulator "
        "offline, to this HDF5 file (see README.md \"Debug HDF5\")");
    program.add_argument("--debug-h5-failed-only").flag().help("with --debug-h5, record only frames that fail CRC");
    program.add_argument("--spectrum").flag().help(
        "compute and publish a periodic FFT of the raw pre-resample IQ over --ws-port, for a UI "
        "waterfall (off by default -- the FFT is wasted work when nothing's listening for it)");
}

DemodLoopParams DemodLoopParams::from_options(const argparse::ArgumentParser& program, std::string command_line) {
    return {
        .preamble_min = program.get<float>("--preamble-min"),
        .slice_mag_min = program.get<float>("--slice-mag-min"),
        .enable_spectrum = program.get<bool>("--spectrum"),
        .debug =
            {
                .path = program.present("--debug-h5"),
                .failed_only = program.get<bool>("--debug-h5-failed-only"),
                .command_line = std::move(command_line),
            },
    };
}

namespace {

// Demodulated blocks demodulate() may run ahead of run_demod_loop by. Each is only its frames, so this can
// cover a long stall in publishing or the history database.
constexpr size_t kDemodQueueBlocks = 64;
// Blocks with frames the debug recorder may fall behind by, and with --debug-h5 the demod queue too: each
// then holds a whole IqBlock.
constexpr size_t kRecordQueueBlocks = 4;

// A block's frames for record_frames(), validated (by handle_frame()), since only run_demod_loop may validate:
// its AircraftTracker keeps the set of confirmed addresses.
struct RecordItem {
    DemodSnapshot snapshot;
    std::vector<AdsbFrame> frames;
    std::vector<DecodedFrameView> views;  // one per frame
};

// Passes every block through, publishing a spectrum frame from one at most every kSpectrumInterval on
// `publish`: its own sender, since it runs in demodulate()'s task, alongside run_demod_loop's publishing.
coro::CoroStream<IqBlock> spectrum_tap(coro::CoroStream<IqBlock> in, coro::BroadcastSender<std::string> publish,
                                       double rate_hz, double freq_hz) {
    SpectrumComputer spectrum;
    auto next_at = std::chrono::steady_clock::now();
    while (auto block = co_await coro::next(in)) {
        if (block->n >= static_cast<size_t>(SpectrumComputer::kFftSize)) {
            if (auto now = std::chrono::steady_clock::now(); now >= next_at) {
                TRACE_ZONE("spectrum_compute");
                std::array<float, 2 * SpectrumComputer::kFftSize> iq;
                block->to_float(static_cast<size_t>(block->lead), iq);
                auto mag_db = spectrum.compute(iq);
                publish.send(nlohmann::json(SpectrumFrame{mag_db, rate_hz, freq_hz}).dump());
                next_at = now + kSpectrumInterval;
            }
        }
        co_yield std::move(*block);
    }
}

// Writes every frame it is sent to the HDF5 debug file, until run_demod_loop stops sending.
coro::Coro<void> record_frames(coro::MpscReceiver<RecordItem> in, FrameRecorder recorder) {
    while (auto item = co_await coro::next(in)) {
        const DemodSnapshot& s = item->snapshot;
        for (size_t i = 0; i < item->frames.size(); i++)
            recorder.record(s.block, s.envelope, s.level, item->frames[i], item->views[i]);
    }
}

}  // namespace

coro::Coro<uint64_t> run_demod_loop(FilterBank filter, double rate_hz, double freq_hz,
                                    coro::CoroStream<IqBlock> stream, DemodLoopParams params,
                                    std::optional<AircraftHistory> history) {
    LOGF(INFO, "demodulator: preamble score threshold %g, bit-slice magnitude threshold %g", params.preamble_min,
         params.slice_mag_min);
    std::optional<coro::MpscSender<RecordItem>> to_recorder;
    coro::JoinHandle<void> recorder;
    if (params.debug.path) {
        LOGF(INFO, "recording %s to %s", params.debug.failed_only ? "the frames that fail CRC" : "every frame",
             *params.debug.path);
        // Created here, so a debug file that can't be created is an error at startup.
        FrameRecorder file(*params.debug.path, demod_params(filter, rate_hz, params.preamble_min, params.slice_mag_min),
                           filter, params.debug.command_line, params.debug.failed_only);
        auto [tx, rx] = coro::mpsc_channel<RecordItem>(kRecordQueueBlocks);
        to_recorder = std::move(tx);
        recorder = coro::build_task().name("debug_h5").spawn(record_frames(std::move(rx), std::move(file)));
    }

    coro::CoroStream<IqBlock> blocks = std::move(stream);
    // Only in the chain when wanted, and with somewhere to publish: building the FFTW plan isn't free, and the
    // whole point of --spectrum being opt-in is to not pay for the FFT when nothing's listening.
    if (params.enable_spectrum) {
        if (auto publish = ws_publisher_sender()) {
            LOGF(INFO, "publishing the spectrum every %lld ms", static_cast<long long>(kSpectrumInterval.count()));
            blocks = spectrum_tap(std::move(blocks), std::move(*publish), rate_hz, freq_hz);
        } else {
            LOGF(WARNING, "--spectrum ignored: there is no --ws-port to publish it on");
        }
    }
    // A task of its own, so the demodulator runs ahead while this loop handles, publishes and records.
    auto demodulated = coro::build_task()
                           .name("demodulate")
                           .buffer(to_recorder ? kRecordQueueBlocks : kDemodQueueBlocks)
                           .spawn(demodulate(std::move(blocks), std::move(filter), rate_hz, params.preamble_min,
                                             params.slice_mag_min, to_recorder.has_value()));

    // ADSB_TIMING set to anything but 0: a throughput report every kStatusReportInterval rather than every
    // block, since a live stream runs for hours and each line is synchronous I/O.
    bool report_timing = false;
    if (const char* env = std::getenv("ADSB_TIMING")) {
        report_timing = std::string_view(env) != "0";
        if (report_timing)
            LOGF(INFO, "ADSB_TIMING=%s: logging the demod loop's throughput every %lld ms", env,
                 static_cast<long long>(kStatusReportInterval.count()));
        else
            LOGF(INFO, "ADSB_TIMING=%s: the demod loop's throughput report is off", env);
    }
    const auto loop_start = std::chrono::steady_clock::now();
    auto next_status_at = loop_start;

    // Sample 0 is taken to arrive now. Right for the live sources (bar clock drift and dropped samples); a
    // file plays back faster than real time, so its aircraft times run ahead of the wall clock.
    const double start_unix_s =
        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();

    AircraftTracker tracker;
    uint64_t sample_count = 0;
    uint64_t frame_count = 0;
    // Logged however the loop ends: at the end of the stream, or with the coroutine frame dropped when
    // main() cancels the task on Ctrl+C.
    struct FrameCountLog {
        const uint64_t& frame_count;
        ~FrameCountLog() { LOGF(INFO, "%u frame(s) decoded", frame_count); }
    } frame_count_log{frame_count};

    while (auto b = co_await coro::next(demodulated)) {
        std::vector<DecodedFrameView> views;
        std::vector<HistoryRow> history_rows;
        {
            // Closed before the sends to the history and the recorder below: a zone can't span a co_await.
            TRACE_ZONE_VALUE("sink_block", b->frames.size());
            // Everything this block publishes, sent as one ws_publisher_publish().
            std::string published;
            for (const AdsbFrame& f : b->frames) {
                DecodedFrameView v =
                    handle_frame(tracker, f, rate_hz, start_unix_s, published, history ? &history_rows : nullptr);
                if (b->snapshot) views.push_back(v);
            }
            if (!published.empty()) ws_publisher_publish(std::move(published));
        }
        if (!history_rows.empty()) co_await history->insert(std::move(history_rows));
        frame_count += b->frames.size();
        sample_count += b->n;

        if (b->snapshot) {
            RecordItem item{std::move(*b->snapshot), std::move(b->frames), std::move(views)};
            // Fails only once record_frames() has stopped on an error, which awaiting it below rethrows.
            if (!co_await to_recorder->send(std::move(item))) break;
        }

        if (report_timing) {
            auto now = std::chrono::steady_clock::now();
            if (now >= next_status_at) {
                double elapsed_s = std::chrono::duration<double>(now - loop_start).count();
                double samples_per_s = elapsed_s > 0.0 ? static_cast<double>(sample_count) / elapsed_s : 0.0;
                LOGF(INFO, "[timing] %u samples, %u frames, %.3f Msamples/s, realtime factor=%.2fx", sample_count,
                     frame_count, samples_per_s / 1e6, samples_per_s / rate_hz);
                next_status_at = now + kStatusReportInterval;
            }
        }
    }
    // Ends record_frames() once it has written what is queued.
    to_recorder.reset();
    if (recorder.valid()) co_await recorder;
    co_return frame_count;
}
