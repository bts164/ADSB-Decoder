#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <coro/sync/broadcast.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/watch.h>

#include "api/convert.h"
#include "output/aircraft_history.h"
#include "output/capture.h"

/**
 * @file
 * The UI API's server: a WebSocket server whose clients make calls, each answered from what the pipeline
 * publishes on the channels below. doc/design/ui_api.md describes the protocol and the design.
 */

/**
 * One block's frames, or one spectrum row. Shared, since a broadcast keeps each value in its queue for the other
 * receivers, so every receive copies it out: the shared pointer makes that copy a reference count rather than a
 * deep copy.
 */
using LiveItem = std::variant<std::shared_ptr<const std::vector<api::Frame>>, std::shared_ptr<const api::SpectrumRow>>;

/** The aircraft published recently, by ICAO address, each as last published. */
using LiveAircraft = std::unordered_map<uint32_t, api::Aircraft>;

/** What the pipeline's tasks are asked to do, written by the API server. */
struct PipelineSettings {
    int spectrum_viewers = 0;  /**< spectrum calls in progress; the spectrum task computes while above 0 */
};

/**
 * The pipeline's ends of the server's channels: what it publishes to the clients on, and what it reads. Each
 * but `captures` is for one task at a time: clone for another.
 */
struct ApiSenders {
    coro::BroadcastSender<LiveItem> live;
    coro::WatchSender<LiveAircraft> aircraft;
    coro::WatchSender<std::optional<api::InputStatus>> input_status;  /**< unset until the input reports one */
    coro::WatchReceiver<PipelineSettings> settings;
    coro::MpscReceiver<CaptureCall> captures;  /**< the capture calls, for one task only */
};

/** The server's ends. */
struct ApiReceivers {
    coro::BroadcastReceiver<LiveItem> live;
    coro::WatchReceiver<LiveAircraft> aircraft;
    coro::WatchReceiver<std::optional<api::InputStatus>> input_status;
    coro::WatchSender<PipelineSettings> settings;
    coro::MpscSender<CaptureCall> captures;
};

/** Makes the server's channels; each watch starts empty. */
std::pair<ApiSenders, ApiReceivers> api_channels();

/**
 * Starts the server on `port`, on all interfaces, as a detached task the runtime cancels when it shuts down.
 * Call from within the coro runtime.
 * @param port TCP port to listen on
 * @param default_interval time between a subscription's updates, unless its request says otherwise
 * @param live whether there's a pipeline publishing on `inputs`; without one the spectrum call fails with
 *     NO_LIVE_INPUT
 * @param capture whether the pipeline takes capture calls on `inputs` (--capture-dir); without it the capture
 *     call fails with NO_CAPTURE
 * @param inputs what the calls are answered from, and the settings they write
 * @param history what the history calls are answered from; without one they fail with NO_HISTORY
 */
void api_server_spawn(uint16_t port, std::chrono::nanoseconds default_interval, bool live, bool capture,
                      ApiReceivers inputs, std::optional<AircraftHistory> history);
