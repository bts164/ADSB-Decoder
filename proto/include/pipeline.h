#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>

#include <nlohmann/json.hpp>

#include <coro/stream.h>

#include "constants.h"
#include "demod.h"
#include "filter_bank.h"
#include "iq_block.h"
#include "spectrum.h"
#include "ws_publisher.h"

// Glues frame_decode.h (CRC/ICAO extraction) and aircraft.h (DF17/18 state
// tracking) together for one decoded frame: prints/publishes the frame
// itself, and -- for CRC-valid DF17/18 frames -- updates the running
// AircraftState and prints/publishes it too, but only when a displayed field
// actually changed. Shared by both file mode and --rtlsdr streaming mode
// (see main.cpp / rtlsdr_stream.h).
void handle_frame(const AdsbFrame& f, double rate_hz, uint64_t base_sample_index, bool print_stdout);

// Drains IqBlocks off `stream` one at a time until it's exhausted, running
// each through AdsbDemod::exec() and -- when enable_spectrum -- the periodic
// FFT side channel (see spectrum.h), calling handle_frame() per decoded
// frame. This is the live-streaming compute loop shared by every raw-IQ
// source (rtlsdr today, other SDRs or a streaming file reader later); it
// knows nothing about where IqBlocks come from beyond satisfying
// coro::Stream<IqBlock> -- an MpscReceiver<IqBlock>, a CoroStream<IqBlock>
// generator, anything. Meant to run on a coro::spawn_blocking thread (see
// coro::blocking_next()'s runtime-context requirement in
// doc/design/blocking_wait.md in the coro repo), same as its callers.
// Returns the total number of frames decoded.
template<coro::Stream S>
    requires std::same_as<typename S::ItemType, IqBlock>
uint64_t run_demod_loop(FilterBank filter, double rate_hz, float preamble_min, float slice_mag_min, S stream,
                         bool print_stdout, double freq_hz, bool enable_spectrum) {
    AdsbDemod demod(filter, rate_hz, preamble_min, slice_mag_min);
    // Only constructed when actually wanted: building the FFTW plan isn't
    // free, and the whole point of --spectrum being opt-in is to not pay
    // for this when nothing's listening.
    std::optional<SpectrumComputer> spectrum;
    if (enable_spectrum) spectrum.emplace();
    auto next_spectrum_at = std::chrono::steady_clock::now();

    // Opt-in (ADSB_TIMING env var) periodic throughput report, shared by
    // every Stream<IqBlock> source: rate-limited via kStatusReportInterval
    // rather than printed per block, since a continuous --rtlsdr stream can
    // run for hours and per-block printing would both be noisy and (being
    // synchronous stderr I/O on the same thread that's decoding) a real
    // drag on throughput.
    const bool report_timing = std::getenv("ADSB_TIMING") != nullptr;
    const auto loop_start = std::chrono::steady_clock::now();
    auto next_status_at = loop_start;

    uint64_t stream_offset = 0;
    uint64_t frame_count = 0;
    while (auto block = coro::blocking_next(stream)) {
        if (spectrum && block->n >= static_cast<size_t>(kSpectrumFftSize)) {
            auto now = std::chrono::steady_clock::now();
            if (now >= next_spectrum_at) {
                auto mag_db = spectrum->compute(block->samples.data() + block->lead);
                ws_publisher_publish(nlohmann::json(SpectrumFrame{mag_db, rate_hz, freq_hz}).dump());
                next_spectrum_at = now + kSpectrumInterval;
            }
        }

        auto frames = demod.exec(block->samples.data() + block->lead, block->n);
        for (const auto& f : frames) handle_frame(f, rate_hz, stream_offset, print_stdout);
        frame_count += frames.size();
        stream_offset += block->n;

        if (report_timing) {
            auto now = std::chrono::steady_clock::now();
            if (now >= next_status_at) {
                double elapsed_s = std::chrono::duration<double>(now - loop_start).count();
                double samples_per_s = elapsed_s > 0.0 ? static_cast<double>(stream_offset) / elapsed_s : 0.0;
                std::cerr << "[timing] " << stream_offset << " samples, " << frame_count << " frames, "
                          << samples_per_s / 1e6 << " Msamples/s, realtime factor=" << samples_per_s / rate_hz
                          << "x\n";
                next_status_at = now + kStatusReportInterval;
            }
        }
    }
    return frame_count;
}
