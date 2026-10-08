#pragma once

// vita49_send's simulated source: renders ADS-B traffic in-process and streams it as VRT packets paced to real
// time.

#include <cstddef>
#include <cstdint>
#include <memory>

#include "apps/vita49_send/packet_sender.h"
#include "sim/adsb_sim.h"

#include <coro/coro_stream.h>
#include <coro/sync/watch.h>

namespace vita49_send {

struct SimStreamParams {
    adsb_sim::Config sim;
    size_t samples_per_packet;
    uint32_t stream_id;
    double duration_s;  // <= 0: until SIGINT
    double rf_freq_hz;  // advertised in the context packet; the simulated signal is baseband
    double context_interval_s;
    double queue_s;  // how far ahead of real time the generator may render
    size_t batch_packets;  // data packets per batch; more than 1 needs UDP GSO on the socket (enable_gso())
};

// A stream of simulated ADS-B IQ as batches of VRT packets, paced to real time the way a capture device would
// be: each batch is released once the wall clock reaches its last sample. Two stages, so that rendering and
// the consumer's sending run in parallel on separate cores:
//
//  * a generator on a blocking thread renders the signal and queues it a batch at a time, up to
//    params.queue_s of signal ahead of real time. With params.sim.render_threads > 1 it renders a few
//    batches per step and splits the work with that many threads;
//  * the stream itself takes each batch off the queue and holds it back until it is due.
//
// Releasing one batch at a time keeps bursts short (~80-160 us of signal at 100-200 Msps), which matters
// because on loopback a GSO send arrives at the receiver all at once. When the consumer runs late (descheduled
// for a few ms), it catches up by taking the queued backlog back to back, at send-only speed since it is
// already rendered, like a capture device draining its FIFO: that burst needs a receive buffer big enough for
// the stall (net.core.rmem_max/rmem_default raised; a 3 ms stall at 200 Msps is ~2.4 MB). Whichever stage
// falls more than 100 ms behind drops the late samples, like a FIFO overrun, and counts them on `paceTx`. The
// IQ content at a given timestamp depends only on the sim config, not on this pacing.
//
// Ends after params.duration_s of signal, or on SIGINT.
coro::CoroStream<PacketBatch> sim_packet_stream(std::unique_ptr<adsb_sim::Simulator> sim, SimStreamParams params,
                                                coro::WatchSender<PaceStats> paceTx);

}  // namespace vita49_send
