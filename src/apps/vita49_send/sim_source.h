#pragma once

// vita49_send's simulated source: renders ADS-B traffic in-process and streams it as VRT packets paced to real
// time.

#include <cstddef>
#include <cstdint>
#include <string>

#include <argparse/argparse.hpp>

#include "apps/vita49_send/pace_stats.h"
#include "apps/vita49_send/packet_batch.h"
#include "sim/adsb_sim.h"

#include <coro/coro_stream.h>
#include <coro/sync/watch.h>

struct SimStreamParams {
    adsb_sim::Config sim;  // the simulator is built from this
    std::string scenario;  // the file sim.aircraft came from, for the log
    size_t samples_per_packet;
    uint32_t stream_id;
    double duration_s;  // <= 0: until the stream is dropped
    double rf_freq_hz;  // advertised in the context packet; the simulated signal is baseband
    double context_interval_s;
    double queue_s;  // how far ahead of real time the generator may render

    // The sim subcommand's options: scenario, duration, signal model, rendering and queue.
    static void add_options(argparse::ArgumentParser& cmd);
    // Loads the scenario too. `program` is the top-level parser, for the options every source shares; `cmd` is
    // the sim subcommand's. Throws std::invalid_argument for a bad option or scenario.
    static SimStreamParams from_options(const argparse::ArgumentParser& program, const argparse::ArgumentParser& cmd);
};

// A stream of simulated ADS-B IQ as batches of VRT packets, paced to real time the way a capture device would
// be: each batch is released once the wall clock reaches its last sample. Two stages, each a task on the
// runtime, so that rendering and the consumer's sending run in parallel:
//
//  * a generator renders the signal and queues it a batch at a time, up to params.queue_s of signal ahead of
//    real time. With params.sim.render_threads > 1 it renders a few batches per step and splits each step
//    into that many coro::parallel() parts (see Simulator::generate()), which render straight into the
//    packets' payloads;
//  * the stream itself takes each batch off the queue and holds it back until it is due.
//
// Releasing one batch at a time keeps bursts short (~80-160 us of signal at 100-200 Msps), which matters
// because on loopback a GSO send arrives at the receiver all at once. When the consumer runs late (descheduled
// for a few ms), it catches up by taking the queued backlog back to back, at send-only speed since it is
// already rendered, like a capture device draining its FIFO: that burst needs a receive buffer big enough for
// the stall (net.core.rmem_max/rmem_default raised; a 3 ms stall at 200 Msps is ~2.4 MB). Whichever stage
// falls more than 100 ms behind drops the late samples, like a FIFO overrun, and counts them on `pace_tx`. The
// IQ content at a given timestamp depends only on the sim config, not on this pacing.
//
// Each batch holds up to `batch_packets` data packets; more than 1 needs UDP GSO on the socket
// (net::enable_gso()). Builds the simulator from params.sim, and logs it, before returning; throws
// std::invalid_argument from there for a bad config. Ends after params.duration_s of signal. Dropping the stream
// cancels the generator.
coro::CoroStream<PacketBatch> sim_packet_stream(SimStreamParams params, size_t batch_packets,
                                                coro::WatchSender<PaceStats> pace_tx);
