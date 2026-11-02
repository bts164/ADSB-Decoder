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
#include <coro/stream.h>
#include <coro/sync/broadcast.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/watch.h>
#include <coro/task/join_handle.h>
#include <coro/task/spawn_builder.h>

#include <absl/strings/str_format.h>

#include "common/log.h"
#include "common/trace.h"
#include "decode/aircraft.h"
#include "dsp/spectrum.h"
#ifdef ADSB_WITH_HDF5
#include "output/frame_recorder.h"
#endif

namespace {

// Longest an aircraft goes unpublished while frames from it keep arriving.
constexpr double kAircraftRefreshS = 1.0;

// Interval between the pipeline's `ADSB_TIMING` throughput reports.
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
    };
}

namespace {

// Demodulated blocks demodulate() may run ahead of the sink by. Each is only its frames, so this can
// cover a long stall in publishing or the history database.
constexpr size_t kDemodQueueBlocks = 64;
// Blocks a capture may fall behind by, on the input and envelope broadcasts. Each holds a block's buffer, or
// its envelope, until every capture has written it.
constexpr size_t kCaptureQueueBlocks = 8;
// Blocks' frames, or spectrum rows, a broadcast holds for a call that hasn't taken them yet.
constexpr size_t kLiveQueueItems = 64;
// Blocks with frames the debug recorder may fall behind by, and with --debug-h5 the demod queue too: each
// then holds a block's buffer, which its source can't reuse until the recorder is done with it.
constexpr size_t kRecordQueueBlocks = 4;

// A block's frames for record_frames(), validated (by handle_frame()), since only the sink may validate:
// its AircraftTracker keeps the set of confirmed addresses.
struct RecordItem {
    DemodSnapshot snapshot;
    std::vector<AdsbFrame> frames;
    std::vector<DecodedFrameView> views;  // one per frame
};

// The blocks from `in`, for spectrum_rows(), which takes a stream. `in` outlives the stream, in the spectrum
// task's frame.
coro::CoroStream<IqBlock> blocks_from(coro::MpscReceiver<IqBlock>& in) {
    while (auto block = co_await coro::next(in)) co_yield std::move(*block);
}

// The spectrum's own task, so its FFTs run on another worker rather than in the demodulator's. Computes once for
// every viewer, and only while `rows` has one: it reads the tee's blocks only then, and its input is inactive,
// so the tee sends nothing, in between. Ends when the input does.
coro::Coro<void> publish_spectrum(coro::InactiveMpscReceiver<IqBlock> idle,
                                  coro::BroadcastSender<LiveSpectrumRow> rows, double rate_hz, double freq_hz) {
    for (;;) {
        co_await rows.receivers_active();
        LOGF(INFO, "spectrum: computing, for a client watching it");
        coro::MpscReceiver<IqBlock> in = std::move(idle).activate();
        bool watched = true;
        {
            coro::CoroStream<SpectrumFrame> frames =
                spectrum_rows(blocks_from(in), kSpectrumInterval, rate_hz, freq_hz);
            while (watched) {
                auto row = co_await coro::next(frames);
                if (!row) co_return;
                // Fails once the last viewer has gone. Races with a viewer arriving just after, harmlessly:
                // receivers_active() then resolves at once, and that viewer waits one interval longer.
                watched = rows.send(std::make_shared<const api::SpectrumRow>(to_proto(*row))).has_value();
            }
        }
        LOGF(INFO, "spectrum: stopped, no client watching it");
        // Drops the blocks queued, so their buffers go back to the source.
        idle = std::move(in).deactivate();
    }
}

// Passes every block through, offers a copy to the spectrum task, which takes it unless it's full or inactive,
// and sends one on `input`, which stores nothing while no capture reads it. Neither waits, so neither holds back
// the stream. Runs in demodulate()'s task.
coro::CoroStream<IqBlock> tee(coro::CoroStream<IqBlock> in, coro::MpscSender<IqBlock> spectrum,
                              coro::BroadcastSender<IqBlock> input) {
    while (auto block = co_await coro::next(in)) {
        (void)spectrum.try_send(*block);
        (void)input.send(*block);
        co_yield std::move(*block);
    }
}

#ifdef ADSB_WITH_HDF5
// Writes every frame it is sent to the HDF5 debug file, until the sink stops sending.
coro::Coro<void> record_frames(coro::MpscReceiver<RecordItem> in, FrameRecorder recorder) {
    while (auto item = co_await coro::next(in)) {
        const DemodSnapshot& s = item->snapshot;
        for (size_t i = 0; i < item->frames.size(); i++)
            recorder.record(s.block, s.envelope, s.level, item->frames[i], item->views[i]);
    }
}
#endif

// What the stages take: the senders, the spectrum task's input, and the settings.
struct Stages {
    FilterBank filter;
    double rate_hz;
    double freq_hz;
    DemodLoopParams params;
    std::optional<AircraftHistory> history;
    coro::BroadcastSender<LiveFrames> frames;
    coro::WatchSender<LiveAircraft> aircraft;
    coro::BroadcastSender<LiveSpectrumRow> spectrum;
    coro::MpscSender<IqBlock> spectrum_in;
    coro::InactiveMpscReceiver<IqBlock> spectrum_reader;
    coro::BroadcastSender<IqBlock> input;
    coro::BroadcastSender<EnvelopeShare> envelope;
};

// demod_pipeline()'s coroutine, which runs the stages.
coro::Coro<uint64_t> run_stages(Stages stages, coro::CoroStream<IqBuffer> stream) {
    const double rate_hz = stages.rate_hz;
    const DemodLoopParams& params = stages.params;
    LOGF(INFO, "demodulator: preamble score threshold %g, bit-slice magnitude threshold %g", params.preamble_min,
         params.slice_mag_min);
    std::optional<coro::MpscSender<RecordItem>> to_recorder;
    coro::JoinHandle<void> recorder;
#ifdef ADSB_WITH_HDF5
    if (params.debug.path) {
        LOGF(INFO, "recording %s to %s", params.debug.failed_only ? "the frames that fail CRC" : "every frame",
             *params.debug.path);
        // Created here, so a debug file that can't be created is an error at startup.
        FrameRecorder file(*params.debug.path,
                           demod_params(stages.filter, rate_hz, params.preamble_min, params.slice_mag_min),
                           stages.filter, params.debug.command_line, params.debug.failed_only);
        auto [tx, rx] = coro::mpsc_channel<RecordItem>(kRecordQueueBlocks);
        to_recorder = std::move(tx);
        recorder = coro::build_task().name("debug_h5").spawn(record_frames(std::move(rx), std::move(file)));
    }
#endif

    // Cancelled when this returns and drops it.
    coro::JoinHandle<void> spectrum = coro::build_task().name("spectrum").spawn(
        publish_spectrum(std::move(stages.spectrum_reader), std::move(stages.spectrum), rate_hz, stages.freq_hz));
    coro::CoroStream<IqBlock> blocks =
        tee(stitch(std::move(stream)), std::move(stages.spectrum_in), std::move(stages.input));
    // A task of its own, so the demodulator runs ahead while the sink handles, publishes and records.
    auto demodulated = coro::build_task()
                           .name("demodulate")
                           .buffer(to_recorder ? kRecordQueueBlocks : kDemodQueueBlocks)
                           .spawn(demodulate(std::move(blocks), std::move(stages.filter), rate_hz,
                                             params.preamble_min, params.slice_mag_min, to_recorder.has_value(),
                                             std::move(stages.envelope)));

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
            // Races with a frames call starting or ending, harmlessly: a new call gets frames from the next block
            // on, and a send after the last has gone fails.
            const bool publish_frames = !stages.frames.all_receivers_dropped();
            std::vector<api::Frame> frames;
            for (const AdsbFrame& f : b->frames) {
                DecodedFrameView v =
                    handle_frame(tracker, f, rate_hz, start_unix_s, publish_frames ? &frames : nullptr, &aircraft);
                if (b->snapshot) views.push_back(v);
            }
            // Every block, with or without frames, so a capture knows when the decoding has passed its end. Fails
            // only with no client to receive it, which is fine.
            if (publish_frames) {
                std::vector<uint64_t> envelope_index;
                envelope_index.reserve(b->frames.size());
                // handle_frame() publishes one Frame per AdsbFrame, so these line up with `frames`.
                for (const AdsbFrame& f : b->frames)
                    envelope_index.push_back(b->envelope_start + static_cast<uint64_t>(f.output_index));
                (void)stages.frames.send(std::make_shared<const FrameBlock>(FrameBlock{
                    b->start + b->n, b->envelope_end, std::move(frames), std::move(envelope_index)}));
            }
            if (!aircraft.empty()) {
                auto live = stages.aircraft.borrow_mut();
                for (const api::Aircraft& ac : aircraft) (*live)[ac.icao()] = ac;
                const double oldest = aircraft.back().last_seen_unix_s() - kLiveAircraftWindowS;
                std::erase_if(*live,
                              [oldest](const auto& entry) { return entry.second.last_seen_unix_s() < oldest; });
            }
        }
        if (stages.history && !aircraft.empty()) co_await stages.history->insert(std::move(aircraft));
        frame_count += b->frames.size();
        sample_count += b->n;
        if (const double now_s = static_cast<double>(sample_count) / rate_hz; now_s - lost_checked_s >= kLostCheckS) {
            log_lost(tracker, lost_checked_s - kLiveAircraftWindowS, now_s - kLiveAircraftWindowS, now_s);
            lost_checked_s = now_s;
        }

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

}  // namespace

DemodPipeline demod_pipeline(FilterBank filter, double rate_hz, double freq_hz, std::optional<double> full_scale,
                             coro::CoroStream<IqBuffer> stream, DemodLoopParams params,
                             std::optional<AircraftHistory> history) {
    // Each channel's own receiver is dropped here: the outputs hold inactive ones, which don't count.
    auto [frames, frames_rx] = coro::broadcast_channel<LiveFrames>(kLiveQueueItems);
    auto [aircraft, aircraft_rx] = coro::watch_channel(LiveAircraft{});
    auto [spectrum, spectrum_rx] = coro::broadcast_channel<LiveSpectrumRow>(kLiveQueueItems);
    auto [spectrum_in, spectrum_reader] = coro::mpsc_channel<IqBlock>(kSpectrumQueueBlocks);
    auto [input, input_rx] = coro::broadcast_channel<IqBlock>(kCaptureQueueBlocks);
    auto [envelope, envelope_rx] = coro::broadcast_channel<EnvelopeShare>(kCaptureQueueBlocks);
    PipelineOutputs outputs{
        .frames = frames.inactive(),
        .aircraft = std::move(aircraft_rx),
        .spectrum = spectrum.inactive(),
        .input = input.inactive(),
        .envelope = envelope.inactive(),
        .input_point = {"input", "capture input missing", rate_hz, false, freq_hz, full_scale},
        .envelope_point = {"envelope", "capture envelope missing", demod_output_rate_hz(), true, {}},
    };
    // A coroutine starts suspended, so nothing runs until the caller spawns it.
    coro::Coro<uint64_t> run = run_stages(
        Stages{
            .filter = std::move(filter),
            .rate_hz = rate_hz,
            .freq_hz = freq_hz,
            .params = std::move(params),
            .history = std::move(history),
            .frames = std::move(frames),
            .aircraft = std::move(aircraft),
            .spectrum = std::move(spectrum),
            .spectrum_in = std::move(spectrum_in),
            .spectrum_reader = std::move(spectrum_reader).deactivate(),
            .input = std::move(input),
            .envelope = std::move(envelope),
        },
        std::move(stream));
    return {.outputs = std::move(outputs), .run = std::move(run)};
}
