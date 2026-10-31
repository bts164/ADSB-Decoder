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
#include "output/api_server.h"

/**
 * @file
 * The `adsb` pipeline: IQ blocks in, frames and aircraft out to its outputs, the history and the log.
 */

/**
 * Handles one demodulated frame: validates it (AircraftTracker::view), logs it, adds it with its
 * frame_summary() to `frames`, and for a frame with a confirmed address updates its AircraftState: decoded
 * fields (update_aircraft_from_frame), and message count, signal level and last-seen time from any DF. The
 * aircraft is added to `aircraft` when a decoded field changed, or at most once a second for the rest; it's
 * logged only for the former.
 *
 * An aircraft heard for the first time, or again after 10 minutes unheard, is logged at INFO. Aircraft
 * changes are logged at --verbosity 2, and every frame at 3.
 * @param tracker the stream's aircraft, updated with this frame
 * @param f frame from demodulate()
 * @param rate_hz input sample rate, for the frame's stream time
 * @param start_unix_s wall-clock time of stream sample 0 (Unix seconds), for AircraftState::last_seen_unix_s
 * @param frames frames to publish for this frame's block, appended to; nullptr when nothing is published
 * @param aircraft aircraft updates to publish and record for this frame's block, appended to; nullptr when they
 *     go nowhere
 * @return the validated frame
 */
DecodedFrameView handle_frame(AircraftTracker& tracker, const AdsbFrame& f, double rate_hz, double start_unix_s,
                              std::vector<api::Frame>* frames, std::vector<api::Aircraft>* aircraft);

/** demod_pipeline()'s optional HDF5 debug recording (FrameRecorder). Never set in a build without ADSB_WITH_HDF5. */
struct DebugRecordOptions {
    std::optional<std::string> path;  /**< HDF5 file to write; no recording without one */
    bool failed_only = false;         /**< record only frames that fail CRC */
    std::string command_line;
};

/** demod_pipeline()'s settings that come from the command line, the same for every source. */
struct DemodLoopParams {
    float preamble_min;    /**< demodulate() preamble threshold */
    float slice_mag_min;   /**< demodulate() slice-magnitude threshold */
    DebugRecordOptions debug;

    /** Adds the thresholds, and --debug-h5* in a build with ADSB_WITH_HDF5. */
    static void add_options(argparse::ArgumentParser& program);
    /** @param command_line the whole command line, recorded in the --debug-h5 file */
    static DemodLoopParams from_options(const argparse::ArgumentParser& program, std::string command_line);
};

/** What demod_pipeline() returns: handles to its outputs, and the coroutine that runs it. */
struct DemodPipeline {
    PipelineOutputs outputs;   /**< for the server; they close when `run` ends */
    coro::Coro<uint64_t> run;  /**< not yet started; @return total number of frames decoded */
};

/**
 * Demodulates an IQ stream through a chain of stages, and publishes what they make on outputs it creates
 * (PipelineOutputs). Each stage does the work behind an output only while that output has active receivers:
 *
 * 1. stitch(), which fills each buffer's padding from its neighbors and yields it as an IqBlock. It holds each
 *    buffer until the next arrives, so frames come out one block late.
 * 2. A tee, which passes each block on, sends a copy on the `input` broadcast, and offers one to the spectrum
 *    task, which misses those that arrive while 2 already wait. While its rows have readers, the spectrum task
 *    publishes a peak-held row on `spectrum` every kSpectrumInterval.
 * 3. demodulate(), in a task of its own with a buffered output, so the next blocks demodulate while this one's
 *    frames are handled. It sends each block's envelope on `envelope` while that has readers.
 * 4. The sink: handle_frame() on each frame, in order. It then sends the block's frames on `frames`, while they
 *    have readers, and updates its aircraft in `aircraft`, dropping those not heard from in 10 minutes; with a
 *    history, one AircraftHistory::insert() of its aircraft.
 *
 * With a debug path, the sink also sends each block that has frames, with its envelope and validated frames,
 * to a FrameRecorder task over a channel. Each of those holds its block's buffer, so the demod task's buffer is
 * shorter then.
 *
 * A coroutine can't hand back its outputs before it runs, so this creates the channels and returns their
 * handles with the coroutine, unstarted, as mpsc_channel() returns its two ends. Spawn `run` as a task on the
 * runtime: demodulate() splits each block's kernels across the runtime's workers, so it needs a multi-threaded
 * runtime to run them in parallel. Cancelling the task (Ctrl+C) drops it, and the stages' tasks with it, at its
 * next co_await. It logs the frame count however it ends. With `ADSB_TIMING` set, it logs throughput and
 * realtime factor every kStatusReportInterval.
 * @param filter polyphase filter bank for demodulate()
 * @param rate_hz input sample rate
 * @param freq_hz center frequency, for labelling spectrum rows and input captures
 * @param stream IQ buffers from any `adsb` source, padded to demod_padding()
 * @param params the thresholds, and the HDF5 debug recording if set
 * @param history where the aircraft updates are recorded, if anywhere
 */
[[nodiscard]] DemodPipeline demod_pipeline(FilterBank filter, double rate_hz, double freq_hz,
                                           coro::CoroStream<IqBuffer> stream, DemodLoopParams params,
                                           std::optional<AircraftHistory> history);
