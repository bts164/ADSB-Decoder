#pragma once

// vita49_send's simulated source: renders ADS-B traffic in-process and streams it as VRT packets paced to real
// time.

#include <cstddef>
#include <cstdint>
#include <memory>

#include "sim/adsb_sim.h"

#include <coro/coro.h>
#include <coro/io/udp_socket.h>

namespace vita49_send {

struct SimStreamParams {
    adsb_sim::Config sim;
    size_t samples_per_packet;
    uint32_t stream_id;
    double duration_s;  // <= 0: until SIGINT
    double rf_freq_hz;  // advertised in the context packet; the simulated signal is baseband
    double context_interval_s;
    double queue_s;  // how far ahead of real time the generator may render (sim_send_loop)
};

// Generates simulated ADS-B IQ and sends it as VRT packets to `sock` (already connected), paced to real time
// the way a capture device would. Two stages, so rendering and sending run in parallel on two cores:
//
//  * a generator on a blocking thread renders one GSO batch at a time and queues it, up to
//    params.queue_s of signal ahead of real time;
//  * this task sends each batch once the wall clock reaches its last sample.
//
// Sending one batch at a time keeps bursts short (~80-160 us of signal at 100-200 Msps), which matters because
// on loopback a GSO send arrives at the receiver all at once. When the sender runs late (descheduled for a few
// ms), it catches up by sending the queued backlog back to back, at send-only speed since it is already
// rendered, like a capture device draining its FIFO: that burst needs a receive buffer big enough for the stall
// (net.core.rmem_max/rmem_default raised; a 3 ms stall at 200 Msps is ~2.4 MB). Whichever stage falls more
// than 100 ms behind drops the late samples, like a FIFO overrun, and they are reported. The IQ content at
// a given timestamp depends only on the sim config, not on this pacing.
//
// Returns after params.duration_s of signal, or on SIGINT.
coro::Coro<void> sim_send_loop(coro::UdpSocket sock, std::unique_ptr<adsb_sim::Simulator> sim, SimStreamParams params);

}  // namespace vita49_send
