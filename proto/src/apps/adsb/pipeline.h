#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <coro/stream.h>

#include "decode/frame_decode.h"
#include "dsp/demod.h"
#include "dsp/filter_bank.h"
#include "dsp/iq_block.h"
#include "dsp/spectrum.h"
#include "output/frame_recorder.h"
#include "output/ws_publisher.h"

/**
 * @file
 * The `adsb` demod loop: IQ blocks in, frames and aircraft out to stdout, WebSocket and history.
 */

/**
 * Handles one demodulated frame: validates it (compute_frame_view), prints it, publishes it with its
 * frame_summary(), and for a frame with a confirmed address updates its AircraftState: decoded fields
 * (update_aircraft_from_frame), and
 * message count, signal level and last-seen time from any DF. The aircraft is published and recorded when a
 * decoded field changed, or at most once a second for the rest; it's printed only for the former.
 * @param f frame from AdsbDemod::exec()
 * @param rate_hz input sample rate, for the frame's stream time
 * @param start_unix_s wall-clock time of stream sample 0 (Unix seconds), for AircraftState::last_seen_unix_s
 * @param print_stdout print the frame (and aircraft updates) to stdout
 * @return the validated frame
 */
DecodedFrameView handle_frame(const AdsbFrame& f, double rate_hz, double start_unix_s, bool print_stdout);

/** run_demod_loop's optional HDF5 debug recording (FrameRecorder). */
struct DebugRecordOptions {
    std::string path;          /**< HDF5 file to write; empty disables recording */
    bool failed_only = false;  /**< record only frames that fail CRC */
    std::string command_line;
};

/** Interval between run_demod_loop's `ADSB_TIMING` throughput reports. */
constexpr std::chrono::milliseconds kStatusReportInterval{1000};

/**
 * Runs every IqBlock from `stream` through a BlockStitcher, AdsbDemod and handle_frame() until the stream
 * ends, publishing a spectrum frame at most every kSpectrumInterval when `enable_spectrum` is set. The
 * stitcher holds each block until the next arrives, so frames come out one block late.
 *
 * Runs on a coro::spawn_blocking thread, since it pulls blocks with coro::blocking_next. With `ADSB_TIMING`
 * set, prints throughput and realtime factor to stderr every kStatusReportInterval.
 * @param filter polyphase filter bank for the AdsbDemod
 * @param rate_hz input sample rate
 * @param preamble_min AdsbDemod preamble threshold
 * @param slice_mag_min AdsbDemod slice-magnitude threshold
 * @param stream any coro::Stream of IqBlock (every `adsb` source), padded to AdsbDemod::padding()
 * @param print_stdout print frames and aircraft updates to stdout
 * @param freq_hz center frequency, for labelling spectrum frames
 * @param enable_spectrum publish spectrum frames
 * @param debug HDF5 debug recording, if its path is set
 * @return total number of frames decoded
 */
template<coro::Stream S>
    requires std::same_as<typename S::ItemType, IqBlock>
uint64_t run_demod_loop(FilterBank filter, double rate_hz, float preamble_min, float slice_mag_min, S stream,
                         bool print_stdout, double freq_hz, bool enable_spectrum, const DebugRecordOptions& debug) {
    AdsbDemod demod(filter, rate_hz, preamble_min, slice_mag_min);
    std::optional<FrameRecorder> recorder;
    if (!debug.path.empty())
        recorder.emplace(debug.path, demod.params(), filter, debug.command_line, debug.failed_only);
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
                auto mag_db = spectrum->compute(block.data());
                ws_publisher_publish(nlohmann::json(SpectrumFrame{mag_db, rate_hz, freq_hz}).dump());
                next_spectrum_at = now + kSpectrumInterval;
            }
        }

        auto frames = demod.exec(block);
        for (const auto& f : frames) {
            DecodedFrameView v = handle_frame(f, rate_hz, start_unix_s, print_stdout);
            if (recorder) recorder->record(demod, block, f, v);
        }
        frame_count += frames.size();
        sample_count += block.n;

        if (report_timing) {
            auto now = std::chrono::steady_clock::now();
            if (now >= next_status_at) {
                double elapsed_s = std::chrono::duration<double>(now - loop_start).count();
                double samples_per_s = elapsed_s > 0.0 ? static_cast<double>(sample_count) / elapsed_s : 0.0;
                std::cerr << "[timing] " << sample_count << " samples, " << frame_count << " frames, "
                          << samples_per_s / 1e6 << " Msamples/s, realtime factor=" << samples_per_s / rate_hz
                          << "x\n";
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
