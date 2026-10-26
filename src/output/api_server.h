#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
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
 * One block's frames, and one spectrum row, as the broadcasts carry them. Shared, since a broadcast keeps each
 * value in its queue for the other receivers, so every receive copies it out: the shared pointer makes that copy
 * a reference count rather than a deep copy.
 */
using LiveFrames = std::shared_ptr<const std::vector<api::Frame>>;
using LiveSpectrumRow = std::shared_ptr<const api::SpectrumRow>;

/** The aircraft published recently, by ICAO address, each as last published. */
using LiveAircraft = std::unordered_map<uint32_t, api::Aircraft>;

/** What clients' calls need the pipeline to compute, counted by the API server. */
struct PipelineDemand {
    int spectrum_viewers = 0;  /**< WatchSpectrum calls in progress; the spectrum tap computes while above 0 */
};

/** Both ends of the settings' watch, which every connection shares. Each is for one task at a time: clone. */
struct SettingsWatch {
    coro::WatchSender<api::Settings> tx;
    coro::WatchReceiver<api::Settings> rx;

    SettingsWatch clone() const { return {tx.clone(), rx.clone()}; }
};

/**
 * The pipeline's ends of the server's channels: what it publishes to the clients on, and what it reads. Each
 * but `captures` is for one task at a time: clone for another.
 */
struct ApiSenders {
    coro::BroadcastSender<LiveFrames> frames;
    coro::BroadcastSender<LiveSpectrumRow> spectrum;
    coro::WatchSender<LiveAircraft> aircraft;
    coro::WatchSender<std::optional<api::InputStatus>> input_status;  /**< unset until the input reports one */
    // These two run the other way: the server writes them, and the pipeline reads them.
    coro::WatchReceiver<PipelineDemand> demand;
    coro::WatchReceiver<api::Settings> settings;
    coro::MpscReceiver<CaptureCall> captures;  /**< the capture calls, for one task only */
};

/** The server's ends. */
struct ApiReceivers {
    coro::BroadcastReceiver<LiveFrames> frames;
    coro::BroadcastReceiver<LiveSpectrumRow> spectrum;
    coro::WatchReceiver<LiveAircraft> aircraft;
    coro::WatchReceiver<std::optional<api::InputStatus>> input_status;
    coro::WatchSender<PipelineDemand> demand;
    SettingsWatch settings;
    coro::MpscSender<CaptureCall> captures;

    /** Ends for another task: the broadcasts resubscribed, the rest cloned. */
    ApiReceivers clone() const;
};

/**
 * Makes the server's channels. The aircraft and the input status start empty.
 * @param settings the settings to start with, such as --capture-dir, from resolve_capture_dir()
 */
std::pair<ApiSenders, ApiReceivers> api_channels(api::Settings settings);

/**
 * Starts the server on `port`, on all interfaces, as a detached task the runtime cancels when it shuts down. It
 * serves `Receiver` with a pipeline and `History` with a database. Call from within the coro runtime.
 * @param port TCP port to listen on
 * @param default_interval time between a watch's replies, unless its request says otherwise
 * @param live the pipeline's channels, from api_channels(); unset without a pipeline
 * @param source the input's subcommand, for ReceiverInfo
 * @param history what the history calls are answered from; unset without a database
 */
void api_server_spawn(uint16_t port, std::chrono::nanoseconds default_interval, std::optional<ApiReceivers> live,
                      std::string source, std::optional<AircraftHistory> history);
