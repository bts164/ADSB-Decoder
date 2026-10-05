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
 * The `adsb` demod loop: IQ blocks in, frames and aircraft out to the WebSocket, history and log.
 */

/**
 * Handles one demodulated frame: validates it (compute_frame_view), logs it, publishes it with its
 * frame_summary(), and for a frame with a confirmed address updates its AircraftState: decoded fields
 * (update_aircraft_from_frame), and
 * message count, signal level and last-seen time from any DF. The aircraft is published and recorded when a
 * decoded field changed, or at most once a second for the rest; it's logged only for the former.
 *
 * The log lines are verbose ones, off by default: aircraft changes at --verbosity 1, every frame at 2.
 * @param f frame from AdsbDemod::exec()
 * @param rate_hz input sample rate, for the frame's stream time
 * @param start_unix_s wall-clock time of stream sample 0 (Unix seconds), for AircraftState::last_seen_unix_s
 * @return the validated frame
 */
DecodedFrameView handle_frame(const AdsbFrame& f, double rate_hz, double start_unix_s);

/** run_demod_loop's optional HDF5 debug recording (FrameRecorder). */
struct DebugRecordOptions {
    std::optional<std::string> path;  /**< HDF5 file to write; no recording without one */
    bool failed_only = false;         /**< record only frames that fail CRC */
    std::string command_line;
};

/**
 * Runs every IqBlock from `stream` through a BlockStitcher, AdsbDemod and handle_frame() until the stream
 * ends, publishing a spectrum frame at most every kSpectrumInterval when `enable_spectrum` is set. The
 * stitcher holds each block until the next arrives, so frames come out one block late.
 *
 * Runs on a coro::spawn_blocking thread, since it pulls blocks with coro::blocking_next. With `ADSB_TIMING`
 * set, logs throughput and realtime factor every kStatusReportInterval.
 * @param filter polyphase filter bank for the AdsbDemod
 * @param rate_hz input sample rate
 * @param preamble_min AdsbDemod preamble threshold
 * @param slice_mag_min AdsbDemod slice-magnitude threshold
 * @param stream any coro::Stream of IqBlock (every `adsb` source), padded to AdsbDemod::padding()
 * @param freq_hz center frequency, for labelling spectrum frames
 * @param enable_spectrum publish spectrum frames
 * @param debug HDF5 debug recording, if its path is set
 * @return total number of frames decoded
 */
template<coro::Stream S>
    requires std::same_as<typename S::ItemType, IqBlock>
uint64_t run_demod_loop(FilterBank filter, double rate_hz, float preamble_min, float slice_mag_min, S stream,
                         double freq_hz, bool enable_spectrum, const DebugRecordOptions& debug);
