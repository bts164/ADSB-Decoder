#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <argparse/argparse.hpp>

#include <coro/coro.h>
#include <coro/coro_stream.h>

#include "decode/aircraft.h"
#include "decode/frame_decode.h"
#include "dsp/demod.h"
#include "dsp/filter_bank.h"
#include "dsp/iq_block.h"
#include "output/aircraft_history.h"

/**
 * @file
 * The `adsb` demod loop: IQ blocks in, frames and aircraft out to the WebSocket, history and log.
 */

/**
 * Handles one demodulated frame: validates it (AircraftTracker::view), logs it, adds it with its
 * frame_summary() to `out`, and for a frame with a confirmed address updates its AircraftState: decoded fields
 * (update_aircraft_from_frame), and
 * message count, signal level and last-seen time from any DF. The aircraft is added to `out` and
 * `history_rows` when a decoded field changed, or at most once a second for the rest; it's logged only for
 * the former.
 *
 * The log lines are verbose ones, off by default: aircraft changes at --verbosity 1, every frame at 2.
 * @param tracker the stream's aircraft, updated with this frame
 * @param f frame from demodulate()
 * @param rate_hz input sample rate, for the frame's stream time
 * @param start_unix_s wall-clock time of stream sample 0 (Unix seconds), for AircraftState::last_seen_unix_s
 * @param out JSON to publish for this frame's block, appended to: comma-separated objects, as
 *     ws_publisher_publish() takes
 * @param history_rows aircraft updates to record for this frame's block, appended to; nullptr when there is
 *     no history
 * @return the validated frame
 */
DecodedFrameView handle_frame(AircraftTracker& tracker, const AdsbFrame& f, double rate_hz, double start_unix_s,
                              std::string& out, std::vector<HistoryRow>* history_rows);

/** run_demod_loop's optional HDF5 debug recording (FrameRecorder). */
struct DebugRecordOptions {
    std::optional<std::string> path;  /**< HDF5 file to write; no recording without one */
    bool failed_only = false;         /**< record only frames that fail CRC */
    std::string command_line;
};

/** run_demod_loop's settings that come from the command line, the same for every source. */
struct DemodLoopParams {
    float preamble_min;    /**< demodulate() preamble threshold */
    float slice_mag_min;   /**< demodulate() slice-magnitude threshold */
    bool enable_spectrum;  /**< publish spectrum frames */
    DebugRecordOptions debug;

    /** Adds the thresholds, --spectrum and --debug-h5*. */
    static void add_options(argparse::ArgumentParser& program);
    /** @param command_line the whole command line, recorded in the --debug-h5 file */
    static DemodLoopParams from_options(const argparse::ArgumentParser& program, std::string command_line);
};

/**
 * Demodulates every IqBlock from `stream` until it ends, through a chain of stream nodes:
 *
 * 1. With `enable_spectrum`, a tap that publishes a spectrum frame from at most one block every
 *    kSpectrumInterval, on a sender of its own (ws_publisher_sender()).
 * 2. demodulate(), in a task of its own with a buffered output, so the next blocks demodulate while this one's
 *    frames are handled. It holds each block until the next arrives, so frames come out one block late.
 * 3. This task, the sink: handle_frame() on each frame, in order, then one ws_publisher_publish() of the
 *    block's frames and aircraft, and with a `history`, one AircraftHistory::insert() of its aircraft.
 *
 * With a debug path, the sink also sends each block that has frames, with its envelope and validated frames,
 * to a FrameRecorder task over a channel. Each of those holds its block, so the demod task's buffer is
 * shorter then.
 *
 * A task on the runtime: demodulate() splits each block's kernels across the runtime's workers, so it needs a
 * multi-threaded runtime to run them in parallel. Cancelling the task (Ctrl+C) drops it, and the nodes' tasks
 * with it, at its next co_await. Logs the frame count however it ends. With `ADSB_TIMING` set, logs
 * throughput and realtime factor every kStatusReportInterval.
 * @param filter polyphase filter bank for demodulate()
 * @param rate_hz input sample rate
 * @param freq_hz center frequency, for labelling spectrum frames
 * @param stream IQ blocks from any `adsb` source, padded to demod_padding()
 * @param params the thresholds, the spectrum and the HDF5 debug recording, if its path is set
 * @param history where the aircraft updates are recorded, if anywhere
 * @return total number of frames decoded
 */
[[nodiscard]] coro::Coro<uint64_t> run_demod_loop(FilterBank filter, double rate_hz, double freq_hz,
                                                  coro::CoroStream<IqBlock> stream, DemodLoopParams params,
                                                  std::optional<AircraftHistory> history);
