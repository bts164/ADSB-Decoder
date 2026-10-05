#pragma once

#include <cstdint>
#include <string>

/**
 * @file
 * WebSocket server that broadcasts JSON lines to every connected client.
 *
 * ws_publisher_init() spawns a detached task on the coro runtime that accepts clients on a WebSocket
 * listener. Published lines go through a broadcast channel (64 deep) with one receiver per client, so each
 * client is sent every line published after it connects; a client that falls more than 64 lines behind
 * skips the ones it missed. Before joining the broadcast, each new client is sent the latest aircraft
 * history snapshot (aircraft_history_snapshot()); after, a client can send `{"type":"seek","t":<unix s>}`
 * to be sent the snapshot at that time, or `{"type":"aircraft_history","icao":"<hex>","t":<unix s>}` (t
 * optional) to be sent one aircraft's recent history (aircraft_history_aircraft()), or
 * `{"type":"activity","from":<unix s>}` (from optional) to be sent aircraft counts per minute
 * (aircraft_history_activity()).
 *
 * Never initializing is supported: ws_publisher_publish() is then a no-op, so callers needn't check
 * whether publishing is enabled.
 */

/** Starts the server on `port`, on all interfaces. Call from within the coro runtime. */
void ws_publisher_init(uint16_t port);

/** Queues one JSON line for every connected client. A no-op before ws_publisher_init(). */
void ws_publisher_publish(std::string json_line);
