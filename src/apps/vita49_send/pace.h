#pragma once

// vita49_send's pacing stage, for a source with no clock of its own (a file): holds each batch back until the
// wall clock reaches it.

#include "apps/vita49_send/pace_stats.h"
#include "apps/vita49_send/packet_batch.h"

#include <coro/coro_stream.h>
#include <coro/sync/watch.h>

// Releases each batch of `stream` once the wall clock reaches its last sample, at `rate_hz` from the first
// batch, as a capture device would deliver it. A batch asked for more than PaceStats::kOverrunS after it was
// due is dropped, like a FIFO overrun, and counted on `pace_tx` as PaceStats::overrun_late_send. Context
// packets (batches of no samples) are always sent.
coro::CoroStream<PacketBatch> pace(coro::CoroStream<PacketBatch> stream, double rate_hz,
                                   coro::WatchSender<PaceStats> pace_tx);
