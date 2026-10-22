#include "apps/adsb/pipeline.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <coro/coro_stream.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/mpsc.h>
#include <coro/task/join_handle.h>
#include <coro/task/spawn_builder.h>

#include <absl/strings/str_format.h>

#include "common/log.h"
#include "common/trace.h"
#include "decode/aircraft.h"
#include "dsp/spectrum.h"
#include "output/capture.h"
#ifdef ADSB_WITH_HDF5
#include "output/frame_recorder.h"
#endif

namespace {

// Longest an aircraft goes unpublished while frames from it keep arriving.
constexpr double kAircraftRefreshS = 1.0;

// Interval between run_demod_loop's `ADSB_TIMING` throughput reports.
constexpr std::chrono::milliseconds kStatusReportInterval{1000};
// Least time between published spectrum rows. Each is a peak-hold over the FFTs since the last, so a shorter
// interval resolves bursts better in time without missing any.
constexpr std::chrono::milliseconds kSpectrumInterval{50};
// Blocks that may wait for the spectrum task. A block arriving while this many wait isn't in the spectrum, so
// the spectrum covers as much as the task keeps up with, and never holds the demodulator back.
constexpr size_t kSpectrumQueueBlocks = 2;
// How long an aircraft not heard from stays in the published aircraft: as far back as a history snapshot
// reaches, so a new client starts with the same aircraft either way. Also when the log calls it lost.
constexpr double kLiveAircraftWindowS = 600.0;
// Stream time between checks for aircraft that have gone unheard for kLiveAircraftWindowS.
constexpr double kLostCheckS = 60.0;

// "a1b2c3", or "a1b2c3 UAL123" once the callsign is known.
std::string aircraft_name(const AircraftState& ac) {
    return absl::StrFormat("%06x%s%s", ac.icao, ac.callsign ? " " : "", ac.callsign.value_or(""));
}

// Logs each aircraft whose last frame is in [from_s, to_s): the check before missed it, and the next won't
// find it, so each disappearance is logged once.
void log_lost(const AircraftTracker& tracker, double from_s, double to_s, double now_s) {
    for (const auto& [icao, ac] : tracker.all()) {
        if (ac.last_seen_s >= from_s && ac.last_seen_s < to_s)
            LOGF(INFO, "aircraft %s: lost, not heard for %.0f min (%u messages)", aircraft_name(ac),
                 (now_s - ac.last_seen_s) / 60, ac.messages);
    }
}

}  // namespace

DecodedFrameView handle_frame(AircraftTracker& tracker, const AdsbFrame& f, double rate_hz, double start_unix_s,
                              std::vector<api::Frame>* frames, std::vector<api::Aircraft>* aircraft) {
    DecodedFrameView v = tracker.view(f, rate_hz);
    TRACE_ZONE("handle_frame");
    VLOGF(3, "frame %v", v);
    if (frames) frames->push_back(to_proto(v, frame_summary(v, v.icao_known ? tracker.find(v.icao) : nullptr)));

    // Every frame whose address checks out counts toward its aircraft's
    // messages and signal level, and marks it seen.
    if (v.crc_ok && v.icao_known) {
        double t_s = static_cast<double>(v.idx) / rate_hz;
        const AircraftState* before = tracker.find(v.icao);
        const double unheard_s = before ? t_s - before->last_seen_s : 0;
        AircraftState& ac = tracker.aircraft(v.icao);
        if (!before) LOGF(INFO, "aircraft %s: heard", aircraft_name(ac));
        else if (unheard_s >= kLiveAircraftWindowS)
            LOGF(INFO, "aircraft %s: heard again after %.0f min", aircraft_name(ac), unheard_s / 60);
        const bool changed = update_aircraft_from_frame(ac, v.df, v.payload, t_s);
        update_aircraft_reception(ac, v.confidence, v.num_bits);
        ac.last_seen_s = t_s;
        ac.last_seen_unix_s = start_unix_s + t_s;
        // The counters and last-seen time change with every frame, so they
        // go out with the next real change or at most once a second.
        if (changed) VLOGF(2, "aircraft %v", ac);
        if (changed || t_s - ac.last_published_s >= kAircraftRefreshS) {
            ac.last_published_s = t_s;
            if (aircraft) aircraft->push_back(to_proto(ac));
        }
    }
    return v;
}

void DemodLoopParams::add_options(argparse::ArgumentParser& program) {
    program.add_argument("--preamble-min").scan<'g', float>().default_value(3.0f).help(
        "preamble correlation score threshold");
    program.add_argument("--slice-mag-min").scan<'g', float>().default_value(2.0f * 56.0f).help(
        "bit-slice magnitude threshold");
    program.add_argument("--capture-dir").help(
        "with --ws-port, take the API's capture calls, which record the input IQ or the demodulator's envelope "
        "for a while as SigMF into this directory (default: no captures)");
#ifdef ADSB_WITH_HDF5
    program.add_argument("--debug-h5").help(
        "write every decoded frame, with its IQ, envelope and the filter and parameters to rerun the demodulator "
        "offline, to this HDF5 file (see README.md \"Debug HDF5\")");
    program.add_argument("--debug-h5-failed-only").flag().help("with --debug-h5, record only frames that fail CRC");
#endif
}

DemodLoopParams DemodLoopParams::from_options(const argparse::ArgumentParser& program, std::string command_line) {
    return {
        .preamble_min = program.get<float>("--preamble-min"),
        .slice_mag_min = program.get<float>("--slice-mag-min"),
        .debug =
            {
#ifdef ADSB_WITH_HDF5
                .path = program.present("--debug-h5"),
                .failed_only = program.get<bool>("--debug-h5-failed-only"),
#else
                .path = std::nullopt,
                .failed_only = false,
#endif
                .command_line = std::move(command_line),
            },
        .capture_dir = program.present("--capture-dir"),
    };
}

namespace {

// Demodulated blocks demodulate() may run ahead of run_demod_loop by. Each is only its frames, so this can
// cover a long stall in publishing or the history database.
constexpr size_t kDemodQueueBlocks = 64;
// Blocks a capture task may fall behind by, in each of its branches. Each holds a block's buffer, or a copy of
// its envelope, until the task has written it, or discarded it while no capture is in progress.
constexpr size_t kCaptureQueueBlocks = 8;
// Capture calls waiting for their capture task, which refuses all but one at a time.
constexpr size_t kCaptureCalls = 2;
// Blocks with frames the debug recorder may fall behind by, and with --debug-h5 the demod queue too: each
// then holds a block's buffer, which its source can't reuse until the recorder is done with it.
constexpr size_t kRecordQueueBlocks = 4;

// A block's frames for record_frames(), validated (by handle_frame()), since only run_demod_loop may validate:
// its AircraftTracker keeps the set of confirmed addresses.
struct RecordItem {
    DemodSnapshot snapshot;
    std::vector<AdsbFrame> frames;
    std::vector<DecodedFrameView> views;  // one per frame
};

// Passes on the blocks from `in` that arrive while `settings` has a spectrum viewer, checking every
// kSpectrumInterval. Peaks held when the last viewer goes are in the first row the next one sees.
coro::CoroStream<IqBlock> while_viewed(coro::MpscReceiver<IqBlock> in,
                                       coro::WatchReceiver<PipelineSettings> settings) {
    auto next_check = std::chrono::steady_clock::now();
    bool computing = false;
    while (auto block = co_await coro::next(in)) {
        if (auto now = std::chrono::steady_clock::now(); now >= next_check) {
            next_check = now + kSpectrumInterval;
            // Races with a spectrum call starting or ending, harmlessly: a new viewer waits up to one interval
            // longer for its first row, and one more row may follow the last viewer's going.
            const int viewers = settings.get().spectrum_viewers;
            if ((viewers > 0) != computing) {
                computing = viewers > 0;
                LOGF(INFO, "spectrum: %s (%d viewer(s))", computing ? "computing" : "stopped", viewers);
            }
        }
        if (computing) co_yield std::move(*block);
    }
}

// The spectrum's own task, so its FFTs run on another worker rather than in the demodulator's. Publishes
// spectrum_rows() of the blocks from `in` on `publish`, while a client watches.
coro::Coro<void> publish_spectrum(coro::MpscReceiver<IqBlock> in, coro::BroadcastSender<LiveItem> publish,
                                  coro::WatchReceiver<PipelineSettings> settings, double rate_hz, double freq_hz) {
    coro::CoroStream<SpectrumFrame> rows =
        spectrum_rows(while_viewed(std::move(in), std::move(settings)), kSpectrumInterval, rate_hz, freq_hz);
    while (auto row = co_await coro::next(rows)) {
        // Fails only with no client to receive it, which is fine.
        (void)publish.send(std::make_shared<const api::SpectrumRow>(to_proto(*row)));
    }
}

// Passes every block through, and sends a share of each to every branch that isn't full, so a branch gets
// as many blocks as it keeps up with and never holds back the stream. Runs in demodulate()'s task.
coro::CoroStream<IqBlock> tee(coro::CoroStream<IqBlock> in, std::vector<coro::MpscSender<IqBlock>> branches) {
    while (auto block = co_await coro::next(in)) {
        for (auto& branch : branches) (void)branch.try_send(block->share());
        co_yield std::move(*block);
    }
}

// Hands each capture call to its point's capture task.
coro::Coro<void> route_captures(coro::MpscReceiver<CaptureCall> calls, coro::MpscSender<CaptureCall> input,
                                coro::MpscSender<CaptureCall> envelope) {
    while (auto call = co_await coro::next(calls)) {
        // The server has checked the point.
        auto& to = call->request.point() == api::CaptureRequest::INPUT ? input : envelope;
        // Fails only once the capture task has stopped, which the call's dropped reply tells its client.
        (void)co_await to.send(std::move(*call));
    }
}

#ifdef ADSB_WITH_HDF5
// Writes every frame it is sent to the HDF5 debug file, until run_demod_loop stops sending.
coro::Coro<void> record_frames(coro::MpscReceiver<RecordItem> in, FrameRecorder recorder) {
    while (auto item = co_await coro::next(in)) {
        const DemodSnapshot& s = item->snapshot;
        for (size_t i = 0; i < item->frames.size(); i++)
            recorder.record(s.block, s.envelope, s.level, item->frames[i], item->views[i]);
    }
}
#endif

}  // namespace

coro::Coro<uint64_t> run_demod_loop(FilterBank filter, double rate_hz, double freq_hz,
                                    coro::CoroStream<IqBuffer> stream, DemodLoopParams params,
                                    std::optional<AircraftHistory> history, std::optional<ApiSenders> ws) {
    LOGF(INFO, "demodulator: preamble score threshold %g, bit-slice magnitude threshold %g", params.preamble_min,
         params.slice_mag_min);
    std::optional<coro::MpscSender<RecordItem>> to_recorder;
    coro::JoinHandle<void> recorder;
#ifdef ADSB_WITH_HDF5
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
#endif

    coro::CoroStream<IqBlock> blocks = stitch(std::move(stream));
    std::vector<coro::MpscSender<IqBlock>> branches;
    coro::JoinHandle<void> spectrum;  // cancelled when this returns and drops it
    if (ws) {
        LOGF(INFO, "spectrum: computed while a client watches it, from as many blocks as it keeps up with");
        auto [tx, rx] = coro::mpsc_channel<IqBlock>(kSpectrumQueueBlocks);
        branches.push_back(std::move(tx));
        spectrum = coro::build_task().name("spectrum").spawn(
            publish_spectrum(std::move(rx), ws->live.clone(), ws->settings.clone(), rate_hz, freq_hz));
    }
    std::optional<coro::MpscSender<EnvelopeRun>> to_envelope_capture;
    std::optional<coro::WatchReceiver<bool>> envelope_wanted;
    std::vector<coro::JoinHandle<void>> captures;  // cancelled when this returns and drops them
    if (ws && params.capture_dir) {
        const double envelope_rate_hz = demod_output_rate_hz();
        LOGF(INFO, "capture: on a capture call, into %s, up to %g s of the input (as the source delivers it, at "
             "%g Hz) or of the envelope (rf32_le at %g Hz)",
             *params.capture_dir, CaptureCall::kMaxDurationS, rate_hz, envelope_rate_hz);
        auto [input_tx, input_rx] = coro::mpsc_channel<IqBlock>(kCaptureQueueBlocks);
        auto [envelope_tx, envelope_rx] = coro::mpsc_channel<EnvelopeRun>(kCaptureQueueBlocks);
        auto [input_calls_tx, input_calls_rx] = coro::mpsc_channel<CaptureCall>(kCaptureCalls);
        auto [envelope_calls_tx, envelope_calls_rx] = coro::mpsc_channel<CaptureCall>(kCaptureCalls);
        auto [wanted_tx, wanted_rx] = coro::watch_channel(false);
        branches.push_back(std::move(input_tx));
        to_envelope_capture = std::move(envelope_tx);
        envelope_wanted = std::move(wanted_rx);
        captures.push_back(coro::build_task().name("capture_input").spawn(
            capture_input(*params.capture_dir, {"input", "capture input missing", rate_hz, freq_hz},
                          std::move(input_rx), std::move(input_calls_rx))));
        captures.push_back(coro::build_task().name("capture_envelope").spawn(
            capture_envelope(*params.capture_dir, {"envelope", "capture envelope missing", envelope_rate_hz, {}},
                             std::move(envelope_rx), std::move(envelope_calls_rx), std::move(wanted_tx))));
        captures.push_back(coro::build_task().name("capture_calls").spawn(route_captures(
            std::move(ws->captures), std::move(input_calls_tx), std::move(envelope_calls_tx))));
    }
    if (!branches.empty()) blocks = tee(std::move(blocks), std::move(branches));
    // A task of its own, so the demodulator runs ahead while this loop handles, publishes and records.
    auto demodulated = coro::build_task()
                           .name("demodulate")
                           .buffer(to_recorder ? kRecordQueueBlocks : kDemodQueueBlocks)
                           .spawn(demodulate(std::move(blocks), std::move(filter), rate_hz, params.preamble_min,
                                             params.slice_mag_min, to_recorder.has_value(),
                                             std::move(envelope_wanted)));

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
    // In stream time, like the aircraft's last-seen times, so a file played faster than real time loses its
    // aircraft at the same points in the stream as it would live.
    double lost_checked_s = 0;
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
        std::vector<api::Aircraft> aircraft;
        {
            // Closed before the sends to the history and the recorder below: a zone can't span a co_await.
            TRACE_ZONE_VALUE("sink_block", b->frames.size());
            std::vector<api::Frame> frames;
            for (const AdsbFrame& f : b->frames) {
                DecodedFrameView v = handle_frame(tracker, f, rate_hz, start_unix_s, ws ? &frames : nullptr,
                                                  ws || history ? &aircraft : nullptr);
                if (b->snapshot) views.push_back(v);
            }
            // Fails only with no client to receive it, which is fine.
            if (ws && !frames.empty())
                (void)ws->live.send(std::make_shared<const std::vector<api::Frame>>(std::move(frames)));
            if (ws && !aircraft.empty()) {
                auto live = ws->aircraft.borrow_mut();
                for (const api::Aircraft& ac : aircraft) (*live)[ac.icao()] = ac;
                const double oldest = aircraft.back().last_seen_unix_s() - kLiveAircraftWindowS;
                std::erase_if(*live,
                              [oldest](const auto& entry) { return entry.second.last_seen_unix_s() < oldest; });
            }
        }
        if (history && !aircraft.empty()) co_await history->insert(std::move(aircraft));
        frame_count += b->frames.size();
        sample_count += b->n;
        if (const double now_s = static_cast<double>(sample_count) / rate_hz; now_s - lost_checked_s >= kLostCheckS) {
            log_lost(tracker, lost_checked_s - kLiveAircraftWindowS, now_s - kLiveAircraftWindowS, now_s);
            lost_checked_s = now_s;
        }

        // A run that doesn't fit is a gap in the capture, which its task logs.
        if (!b->envelope.empty())
            (void)to_envelope_capture->try_send(EnvelopeRun{b->envelope_index, std::move(b->envelope)});

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
