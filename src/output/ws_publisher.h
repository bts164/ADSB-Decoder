#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <coro/sync/broadcast.h>

#include "output/aircraft_history.h"

/**
 * @file
 * WebSocket server that sends published JSON to every connected client in periodic updates.
 *
 * ws_publisher_init() spawns a detached task on the coro runtime that accepts clients on a WebSocket
 * listener. Each ws_publisher_publish() call is one send on a broadcast channel (64 deep) with one receiver
 * per client. A client's task gathers what it receives and, once every update interval, sends it as one
 * message holding a JSON array of the published objects, in the order they were published. Nothing is sent
 * for an interval with nothing published.
 *
 * A client that cannot be sent to goes on gathering. Past `ADSB_WS_MAX_PENDING_BYTES` (default 16 MiB) of
 * waiting text it drops the oldest, and an update that takes over 2 s to send is dropped whole.
 *
 * With a history, before its first update each new client is sent the latest aircraft history snapshot
 * (AircraftHistory::snapshot()); after, a client can send `{"type":"seek","t":<unix s>}`
 * to be sent the snapshot at that time, or `{"type":"aircraft_history","icao":"<hex>","t":<unix s>}` (t
 * optional) to be sent one aircraft's recent history (AircraftHistory::aircraft()), or
 * `{"type":"activity","from":<unix s>}` (from optional) to be sent aircraft counts per minute
 * (AircraftHistory::activity()). The snapshot and each reply is a message holding one JSON object, sent at
 * once, not held for the next update.
 *
 * Never initializing is supported: ws_publisher_publish() is then a no-op, so callers needn't check
 * whether publishing is enabled.
 */

/**
 * Starts the server on `port`, on all interfaces. Call from within the coro runtime.
 * @param port TCP port to listen on
 * @param update_interval time between a client's updates
 * @param history what clients' history requests are answered from; without one they get no reply
 */
void ws_publisher_init(uint16_t port, std::chrono::nanoseconds update_interval,
                       std::optional<AircraftHistory> history);

/**
 * Queues JSON for every connected client's next update. A no-op before ws_publisher_init().
 * @param json_objects one JSON object, or several separated by commas (no brackets): published together,
 *     they cost one channel send
 */
void ws_publisher_publish(std::string json_objects);

/**
 * A sender of its own on the channel ws_publisher_publish() sends on, for a task that publishes alongside its
 * callers: a coro::BroadcastSender is for one thread at a time. Call after ws_publisher_init().
 * @return nullopt before ws_publisher_init(), when nothing is published
 */
std::optional<coro::BroadcastSender<std::string>> ws_publisher_sender();
