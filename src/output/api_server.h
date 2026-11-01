#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <coro/sync/broadcast.h>
#include <coro/sync/watch.h>

#include "api/convert.h"
#include "dsp/demod.h"
#include "dsp/iq_block.h"
#include "output/aircraft_history.h"
#include "output/capture.h"

/**
 * @file
 * The UI API's server: a WebSocket server whose clients make calls, each answered from what the pipeline
 * publishes on its outputs (PipelineOutputs). doc/design/ui_api.md describes the protocol and the design.
 */

/**
 * One block's frames, and one spectrum row, as the broadcasts carry them. Shared, since a broadcast keeps each
 * value in its queue for the other receivers, so every receive copies it out: the shared pointer makes that copy
 * a reference count rather than a deep copy.
 */
using LiveFrames = std::shared_ptr<const std::vector<api::Frame>>;
using LiveSpectrumRow = std::shared_ptr<const api::SpectrumRow>;

/** The aircraft published recently, by ICAO address, each as last published. */
using LiveAircraft = std::unordered_map<uint32_t, api::Aircraft>;

/**
 * Where the server reads the pipeline's data from, one handle per point. A call activates a broadcast's handle
 * for a receiver of its own, and a stage does the work behind its output only while it has active receivers.
 * Each is for one task at a time: clone for another.
 */
struct PipelineOutputs {
    coro::InactiveBroadcastReceiver<LiveFrames> frames;         /**< the sink: each block's frames */
    coro::WatchReceiver<LiveAircraft> aircraft;                 /**< the sink: the aircraft heard in 10 minutes */
    coro::InactiveBroadcastReceiver<LiveSpectrumRow> spectrum;  /**< the spectrum task: peak-held rows */
    coro::InactiveBroadcastReceiver<IqBlock> input;  /**< the tee: every block, in the source's format */
    coro::InactiveBroadcastReceiver<EnvelopeShare> envelope;  /**< demodulate(): the resampled envelope */
    CapturePoint input_point;     /**< how a capture of `input` is labelled */
    CapturePoint envelope_point;  /**< how a capture of `envelope` is labelled */

    PipelineOutputs clone() const;
};

/** What the server's Receiver service answers from. */
struct ReceiverFeeds {
    PipelineOutputs outputs;
    coro::WatchReceiver<std::optional<api::InputStatus>> input_status;  /**< unset until the input reports one */
    coro::WatchReceiver<api::ReceiverPosition> receiver_position;  /**< NO_GPS for an input without a GPS */
    api::Settings settings;  /**< to start with, such as --capture-dir, from resolve_capture_dir() */
};

/**
 * Starts the server on `port`, on all interfaces, as a detached task the runtime cancels when it shuts down. It
 * serves `Receiver` with a pipeline and `History` with a database. Call from within the coro runtime.
 * @param port TCP port to listen on
 * @param default_interval time between a watch's replies, unless its request says otherwise
 * @param live what the Receiver service answers from; unset without a pipeline
 * @param source the input's subcommand, for ReceiverInfo
 * @param history what the history calls are answered from; unset without a database
 */
void api_server_spawn(uint16_t port, std::chrono::nanoseconds default_interval, std::optional<ReceiverFeeds> live,
                      std::string source, std::optional<AircraftHistory> history);
