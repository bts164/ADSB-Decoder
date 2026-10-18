#pragma once

// vita49_send's send stage. A source is a coro::CoroStream<PacketBatch>: main() builds one from the command
// line and hands it to send_loop(), which forwards each batch to the UdpSocket and knows nothing else about
// where the batches come from. How many packets go in a batch, when a batch is released (pacing) and what
// is dropped when the source falls behind are all the source's business.

#include "apps/vita49_send/pace_stats.h"
#include "apps/vita49_send/packet_batch.h"

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/io/udp_socket.h>
#include <coro/sync/watch.h>

// Sends every batch of `stream` to `sock` (already connected) until the stream ends, dropping each batch once
// sent. Publishes `sent` and `send_wait` on `pace_tx` after every batch, runs pace_monitor_loop() meanwhile, and
// logs the run's totals at the end (`rate_hz` is the stream's nominal sample rate).
//
// A failed send throws std::system_error (e.g. ECONNREFUSED, surfaced from an earlier ICMP port-unreachable
// when no listener is bound), which ends the loop; so does anything the stream throws.
coro::Coro<void> send_loop(coro::UdpSocket sock, coro::CoroStream<PacketBatch> stream, double rate_hz,
                           coro::WatchSender<PaceStats> pace_tx, coro::WatchReceiver<PaceStats> pace_rx);
