#include "apps/adsb/pipeline.h"

#include <coro/coro_stream.h>
#include <nlohmann/json.hpp>

#include "common/log.h"
#include "decode/aircraft.h"
#include "output/aircraft_history.h"

namespace {

// Longest an aircraft goes unpublished while frames from it keep arriving.
constexpr double kAircraftRefreshS = 1.0;

// Interval between run_demod_loop's `ADSB_TIMING` throughput reports.
constexpr std::chrono::milliseconds kStatusReportInterval{1000};

}  // namespace

DecodedFrameView handle_frame(const AdsbFrame& f, double rate_hz, double start_unix_s) {
    DecodedFrameView v = compute_frame_view(f, rate_hz);
    VLOGF(2, "frame %v", v);
    nlohmann::json frame = v;
    const auto& table = aircraft_table();
    const auto known = v.icao_known ? table.find(v.icao) : table.end();
    const std::string summary = frame_summary(v, known == table.end() ? nullptr : &known->second);
    frame["summary"] = summary.empty() ? nlohmann::json(nullptr) : nlohmann::json(summary);
    ws_publisher_publish(frame.dump());

    // Every frame whose address checks out counts toward its aircraft's
    // messages and signal level, and marks it seen.
    if (v.crc_ok && v.icao_known) {
        double t_s = static_cast<double>(v.idx) / rate_hz;
        AircraftState& ac = aircraft_table()[v.icao];
        ac.icao = v.icao;
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
            aircraft_history_record(ac, json);
            ws_publisher_publish(std::move(json));
        }
    }
    return v;
}

template<coro::Stream S>
    requires std::same_as<typename S::ItemType, IqBlock>
uint64_t run_demod_loop(FilterBank filter, double rate_hz, float preamble_min, float slice_mag_min, S stream,
                         double freq_hz, bool enable_spectrum, const DebugRecordOptions& debug) {
    AdsbDemod demod(filter, rate_hz, preamble_min, slice_mag_min);
    std::optional<FrameRecorder> recorder;
    if (debug.path)
        recorder.emplace(*debug.path, demod.params(), filter, debug.command_line, debug.failed_only);
    // Only constructed when actually wanted: building the FFTW plan isn't
    // free, and the whole point of --spectrum being opt-in is to not pay
    // for this when nothing's listening.
    std::optional<SpectrumComputer> spectrum;
    if (enable_spectrum) spectrum.emplace();
    auto next_spectrum_at = std::chrono::steady_clock::now();

    // Opt-in (ADSB_TIMING env var) periodic throughput report, shared by
    // every Stream<IqBlock> source: rate-limited via kStatusReportInterval
    // rather than printed per block, since a continuous rtlsdr stream can
    // run for hours and per-block printing would both be noisy and (being
    // synchronous stderr I/O on the same thread that's decoding) a real
    // drag on throughput.
    const bool report_timing = std::getenv("ADSB_TIMING") != nullptr;
    const auto loop_start = std::chrono::steady_clock::now();
    auto next_status_at = loop_start;

    // Sample 0 is taken to arrive now. Right for the live sources (bar clock drift and dropped samples); a
    // file plays back faster than real time, so its aircraft times run ahead of the wall clock.
    const double start_unix_s =
        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();

    BlockStitcher stitcher;
    uint64_t sample_count = 0;
    uint64_t frame_count = 0;
    auto process = [&](const IqBlock& block) {
        if (spectrum && block.n >= static_cast<size_t>(kSpectrumFftSize)) {
            auto now = std::chrono::steady_clock::now();
            if (now >= next_spectrum_at) {
                auto mag_db = spectrum->compute(block.iq().first<2 * kSpectrumFftSize>());
                ws_publisher_publish(nlohmann::json(SpectrumFrame{mag_db, rate_hz, freq_hz}).dump());
                next_spectrum_at = now + kSpectrumInterval;
            }
        }

        auto frames = demod.exec(block);
        for (const auto& f : frames) {
            DecodedFrameView v = handle_frame(f, rate_hz, start_unix_s);
            if (recorder) recorder->record(demod, block, f, v);
        }
        frame_count += frames.size();
        sample_count += block.n;

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
    };
    while (auto block = coro::blocking_next(stream)) {
        if (auto ready = stitcher.push(std::move(*block))) process(*ready);
    }
    if (auto last = stitcher.flush()) process(*last);
    return frame_count;
}
template uint64_t run_demod_loop<coro::CoroStream<IqBlock>>(
    FilterBank filter, double rate_hz, float preamble_min, float slice_mag_min, coro::CoroStream<IqBlock> stream,
    double freq_hz, bool enable_spectrum, const DebugRecordOptions& debug);
