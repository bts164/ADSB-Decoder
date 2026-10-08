// Standalone CLI: streams IQ (from an RTL-SDR, or simulated ADS-B) out as VRT
// (VITA-49) IF Data packets over UDP -- a test source playing the role a
// real VITA-49-capable SDR front-end would, feeding `adsb vita49`
// (input/vita49_stream.h). See
// doc/design/vita49-format.md for the wire format this implements (IF Data
// packets plus periodic IF Context packets carrying bandwidth, RF reference
// frequency and sample rate; no Class ID, no Trailer).
//
// This is deliberately its own executable, not folded into `adsb` -- it has
// nothing to do with demodulation, and its "source" (rtlsdr) and "sink"
// (UDP) are both different from adsb's own IqBlock-consuming pipeline.
//
// Files: this one parses the command line and picks a source; rtlsdr_source.* and
// sim_source.* are the two sources, each a coro::CoroStream<PacketBatch>;
// packet_sender.* sends whichever stream it is given; vrt_encode.* builds the
// packets both sources produce.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <unistd.h>

#include <argparse/argparse.hpp>

#include "apps/log_options.h"
#include "apps/vita49_send/packet_sender.h"
#include "apps/vita49_send/rtlsdr_source.h"
#include "apps/vita49_send/sim_source.h"
#include "common/cpu_layout.h"
#include "sim/adsb_sim.h"

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/io/socket_address.h>
#include <coro/io/udp_socket.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/watch.h>

namespace {

using namespace vita49_send;

coro::Coro<int> async_main(int argc, char** argv) {
    // Options every source shares are global and go before the subcommand; each source's own options belong to its
    // subcommand and go after it:  vita49_send --dest-port 4991 sim --scenario boston.json
    argparse::ArgumentParser program("vita49_send");
    program.add_description("Streams IQ samples as VITA-49 (VRT) IF Data packets over UDP, from a live RTL-SDR or from "
                            "simulated ADS-B traffic. Choose the source with a subcommand: `rtlsdr` or `sim` "
                            "(`vita49_send <subcommand> --help` lists that source's options).");
    program.add_argument("--rate").scan<'g', double>().default_value(2.4e6).help(
        "sample rate in Hz (for the sim source, at least 2 MHz)");
    program.add_argument("--freq").scan<'g', double>().default_value(1090000000.0).help(
        "center frequency in Hz (default: 1090 MHz). Tunes the radio for the rtlsdr source; for the sim source it is "
        "only the RF reference frequency advertised in the context packets");
    program.add_argument("--context-interval").scan<'g', double>().default_value(1.0).help(
        "seconds of signal between IF Context packets (sample rate, RF frequency, bandwidth); "
        "one is always sent before the first data packet. 0 = never send any");
    program.add_argument("--dest-host").default_value(std::string("127.0.0.1")).help(
        "destination host to stream VRT/UDP packets to");
    program.add_argument("--dest-port").scan<'u', unsigned>().required().help(
        "destination UDP port to stream VRT packets to");
    program.add_argument("--stream-id").scan<'u', unsigned>().default_value(1u).help(
        "VRT Stream ID to tag every packet with");
    program.add_argument("--samples-per-packet").scan<'u', unsigned>().default_value(360u).help(
        "IQ sample pairs per VRT packet (default 360, chosen to keep the packet under one "
        "Ethernet MTU -- see doc/design/vita49-format.md's \"UDP framing\" section)");

    add_log_options(program);

    argparse::ArgumentParser rtl_cmd("rtlsdr");
    rtl_cmd.add_description("Stream from a live RTL-SDR.");
    rtl_cmd.add_argument("--device").scan<'d', int>().default_value(0).help("RTL-SDR device index");
    rtl_cmd.add_argument("--gain").scan<'g', double>().default_value(-1.0).help(
        "tuner gain in dB, or negative for auto gain");
    rtl_cmd.add_argument("--buf-num").scan<'u', unsigned>().default_value(0u).help(
        "number of librtlsdr async transfer buffers in flight (0 = librtlsdr default: 15)");
    rtl_cmd.add_argument("--buf-len").scan<'u', unsigned>().default_value(0u).help(
        "librtlsdr async transfer buffer length in bytes, multiple of 512 "
        "(0 = librtlsdr default: 16*32*512 = 262144 bytes)");

    argparse::ArgumentParser sim_cmd("sim");
    sim_cmd.add_description("Stream simulated ADS-B traffic generated in-process.");
    sim_cmd.add_argument("--scenario").required().help(
        "JSON file describing the simulated aircraft (see scenarios/boston.json)");
    sim_cmd.add_argument("--duration").scan<'g', double>().default_value(0.0).help(
        "stop after this many seconds of signal (0 = run until Ctrl+C)");
    sim_cmd.add_argument("--seed").scan<'u', uint64_t>().default_value(uint64_t{1}).help(
        "RNG seed for the noise, initial message timing and carrier phases; same seed, same IQ");
    sim_cmd.add_argument("--snr-db").scan<'g', double>().default_value(30.0).help(
        "pulse power of a full-amplitude aircraft over AWGN power, per complex sample, in dB");
    sim_cmd.add_argument("--pulse-rise-ns").scan<'g', double>().default_value(75.0).help(
        "transmit pulse rise time, 0-100% of a linear edge, in ns");
    sim_cmd.add_argument("--pulse-fall-ns").scan<'g', double>().default_value(100.0).help(
        "transmit pulse fall time in ns");
    sim_cmd.add_argument("--pulse-jitter-ns").scan<'g', double>().default_value(25.0).help(
        "every pulse edge is displaced by a uniform random error of up to +/- this many ns (0 = exact)");
    sim_cmd.add_argument("--rx-cutoff").scan<'g', double>().default_value(0.45).help(
        "receiver anti-alias filter -6 dB cutoff as a fraction of the sample rate (<= 0.5); only "
        "applied at sample rates below 16 MHz, where the signal is rendered oversampled and decimated");
    sim_cmd.add_argument("--oversample").scan<'u', unsigned>().default_value(0u).help(
        "render at this integer multiple of the sample rate before filtering (0 = automatic)");
    sim_cmd.add_argument("--render-threads").scan<'u', unsigned>().default_value(4u).help(
        "threads rendering the signal. add more when the log reports samples dropped as "
        "\"render too slow\". The signal is the same whatever the count");
    sim_cmd.add_argument("--queue-ms").scan<'g', double>().default_value(20.0).help(
        "how far ahead of real time the signal is rendered, in ms: packets queued for sending, so the sender "
        "catches up after a stall at send-only speed. Costs rate * 4 B per second of queue (80 MB per 100 ms at "
        "200 Msps)");

    program.add_subparser(rtl_cmd);
    program.add_subparser(sim_cmd);

    try {
        program.parse_args(argc, argv);
        if (!program.is_subcommand_used(rtl_cmd) && !program.is_subcommand_used(sim_cmd)) {
            throw std::runtime_error("a source subcommand is required: rtlsdr or sim");
        }
    } catch (const std::exception& err) {
        // Show the usage of the parser the bad argument belonged to.
        std::cerr << err.what() << "\n";
        if (program.is_subcommand_used(rtl_cmd)) {
            std::cerr << rtl_cmd;
        } else if (program.is_subcommand_used(sim_cmd)) {
            std::cerr << sim_cmd;
        } else {
            std::cerr << program;
        }
        co_return 1;
    }

    // Argument errors go straight to stderr; everything else is logged.
    try {
        init_logging(program);
    } catch (const std::exception& err) {
        std::cerr << err.what() << "\n";
        co_return 1;
    }

    const double rate_hz = program.get<double>("--rate");
    const double freq_hz = program.get<double>("--freq");
    const double context_interval_s = program.get<double>("--context-interval");
    const std::string dest_host = program.get<std::string>("--dest-host");
    const auto dest_port = static_cast<uint16_t>(program.get<unsigned>("--dest-port"));
    const uint32_t stream_id = program.get<unsigned>("--stream-id");
    const size_t samples_per_packet = program.get<unsigned>("--samples-per-packet");

    auto dest = coro::SocketAddress::parse(dest_host, dest_port);
    if (!dest) {
        std::cerr << "invalid --dest-host: " << dest_host << "\n";
        co_return 1;
    }

    auto sock = co_await coro::UdpSocket::bind("0.0.0.0", 0);
    co_await sock.connect(*dest);
    LOGF(INFO, "streaming VRT/UDP to %s (stream id %u)", dest->to_string(), stream_id);

    // Only the source differs between modes: each hands its coro::CoroStream<PacketBatch> to the same
    // send_loop() call below. The sources report what they drop on the pace statistics.
    auto [paceTx, paceRx] = coro::watch_channel(PaceStats{});
    std::optional<coro::CoroStream<PacketBatch>> stream;
    if (program.is_subcommand_used(sim_cmd)) {
        SimStreamParams params;
        params.sim.sample_rate_hz = static_cast<uint32_t>(rate_hz);
        params.sim.seed = sim_cmd.get<uint64_t>("--seed");
        params.sim.snr_db = sim_cmd.get<double>("--snr-db");
        params.sim.pulse_rise_s = sim_cmd.get<double>("--pulse-rise-ns") * 1e-9;
        params.sim.pulse_fall_s = sim_cmd.get<double>("--pulse-fall-ns") * 1e-9;
        params.sim.pulse_jitter_s = sim_cmd.get<double>("--pulse-jitter-ns") * 1e-9;
        params.sim.rx_cutoff = sim_cmd.get<double>("--rx-cutoff");
        params.sim.oversample = sim_cmd.get<unsigned>("--oversample");
        params.sim.render_threads = sim_cmd.get<unsigned>("--render-threads");
        params.samples_per_packet = samples_per_packet;
        params.stream_id = stream_id;
        params.duration_s = sim_cmd.get<double>("--duration");
        params.rf_freq_hz = freq_hz;
        params.context_interval_s = context_interval_s;
        params.queue_s = sim_cmd.get<double>("--queue-ms") * 1e-3;
        if (!(params.queue_s > 0)) {
            std::cerr << "--queue-ms must be positive\n";
            co_return 1;
        }
        std::unique_ptr<adsb_sim::Simulator> sim;
        try {
            params.sim.aircraft = adsb_sim::load_scenario(sim_cmd.get<std::string>("--scenario"));
            sim = std::make_unique<adsb_sim::Simulator>(params.sim);
        } catch (const std::invalid_argument& err) {
            LOGF(ERROR, "%s", err.what());
            co_return 1;
        }
        LOGF(INFO, "simulating ADS-B: rate=%u Hz, seed=%u, snr=%g dB, %u aircraft%s", params.sim.sample_rate_hz,
             params.sim.seed, params.sim.snr_db, params.sim.aircraft.size(),
             params.duration_s > 0 ? "" : " (Ctrl+C to stop)");
        for (const auto& a : params.sim.aircraft) {
            LOGF(INFO, "  icao=%06x callsign=%.8s lat=%.4f lon=%.4f alt=%dft gs=%.0fkt trk=%.0f vr=%dfpm", a.icao,
                 a.callsign, a.lat_deg, a.lon_deg, a.alt_ft, a.ground_speed_kt, a.track_deg, a.vrate_fpm);
        }
        // Data packets go out up to 44 (of 1468 B) per send via UDP GSO: the kernel's per-datagram work, not
        // the syscall, is what limits a small-datagram sender, and GSO does that work once per send. See the
        // PERFORMANCE NOTE in main().
        params.batch_packets = enable_gso(sock, vrt_packet_bytes(samples_per_packet));
        stream.emplace(sim_packet_stream(std::move(sim), params, paceTx.clone()));
    } else {
        RtlSdrSourceParams params;
        params.device_index = rtl_cmd.get<int>("--device");
        params.rate_hz = rate_hz;
        params.freq_hz = freq_hz;
        params.gain_db = rtl_cmd.get<double>("--gain");
        params.buf_num = rtl_cmd.get<unsigned>("--buf-num");
        params.buf_len = rtl_cmd.get<unsigned>("--buf-len");
        params.samples_per_packet = samples_per_packet;
        params.stream_id = stream_id;
        params.context_interval_s = context_interval_s;
        stream.emplace(rtlsdr_packet_stream(params, paceTx.clone()));
    }

    try {
        co_await send_loop(std::move(sock), std::move(*stream), rate_hz, std::move(paceTx), std::move(paceRx));
    } catch (const std::runtime_error& err) {  // the device couldn't be opened, or a send failed
        LOGF(ERROR, "%s", err.what());
        co_return 1;
    }
    co_return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    // PERFORMANCE NOTE -- READ BEFORE CHANGING (from coro/bench/udp_bench_coro.cpp,
    // loopback, 1460 B datagrams, this two-stage pipeline: rtlsdr callback ->
    // mpsc channel -> encode + UdpSocket::send):
    //
    //  * SINGLE-THREAD RUNTIME (read carefully -- this does NOT automatically
    //    apply to this program). In the benchmark, running BOTH pipeline stages
    //    as coroutine tasks on `coro::Runtime(1)` was ~35% faster than the default
    //    work-stealing `coro::Runtime()` (~443k vs ~286k pkt/s, ~1.55 vs ~2
    //    cores): a same-thread channel hand-off is cheap, a cross-thread one
    //    means wakes plus cache-line ping-pong. HOWEVER here the producer is the
    //    rtlsdr callback on a spawn_blocking thread and the consumer is on a
    //    runtime thread, so the hand-off is cross-thread regardless of runtime
    //    size and Runtime(1) alone will NOT recover that win (it only stops the
    //    consumer migrating between workers; unmeasured). To actually get the
    //    single-thread behavior, remove the thread boundary: encode + send
    //    directly in the rtlsdr callback using a plain blocking POSIX UDP socket
    //    (UdpSocket is coroutine-only), with no channel. Measure before doing so.
    //
    //  * DESIGN NOTE -- why this stays a channel + coroutine pipeline. UDP is lossy
    //    by nature, so a sender that outpaces the socket could simply drop instead
    //    of blocking: keep reading the SDR and skip (or fail) the send, which
    //    would need no channel and no coroutine send at all -- do the encode and a
    //    non-blocking send(MSG_DONTWAIT) right in the rtlsdr callback, never
    //    block it (a stalled callback causes USB overruns, worse than a dropped
    //    UDP packet), and increment the VRT packet counter per ENCODED packet so
    //    receivers can see the gap. That is moot here: an RTL-SDR (~5 MB/s at
    //    2.4 Msps) can never outpace a UDP stream. Likewise most other radios
    //    either originate the VITA-49/UDP stream themselves (FPGA -> UDP; we are
    //    only the receiver) or sit behind USB or similar links that cannot outrun
    //    UDP either. Cases where this WOULD matter: a CPU that gets raw samples
    //    over PCIe from an FPGA and does the VITA-49 framing and UDP itself, or a
    //    fast source (e.g. USB3 radio) forwarded over a slower link (e.g. 1 GbE,
    //    ~125 MB/s), where the send side becomes link-bound and dropping is the
    //    right policy. Revisit this design then.
    //
    //  * Keep `connect()` + `send()` for a single peer; `send_to()` is ~5-10%
    //    slower per packet (kernel route lookup on an unconnected socket).
    //
    //  * Datagram size dominates. 200 Msps (~800 MB/s) needs ~550k pkt/s at 1460 B
    //    -- right at one core's loopback kernel-path limit -- but only ~100k pkt/s
    //    at 8 KB (~3x headroom). Use the largest datagrams the network allows
    //    (jumbo frames) if we ever push toward those rates; --samples-per-packet
    //    is the knob.
    //
    //  * Channel depth: deep channels of large buffers go cache-cold (32 KB
    //    packets: ~201k pkt/s at depth 4 vs ~132k at depth 1024). The 1024-slot
    //    channel in rtlsdr_source.cpp exists to absorb ~365-chunk rtlsdr callback bursts,
    //    which is the right tradeoff at small packet sizes; for large packets use
    //    a shallow channel or recycle a buffer pool instead.
    //
    //  * Batching: the sim source sends data packets with UDP GSO (PacketBatch,
    //    up to 44 x 1468 B per send); the rtlsdr source doesn't need to at
    //    2.4 Msps, and puts one packet in each PacketBatch. Measured bare metal (i5-11500H, loopback, 1468 B, performance
    //    governor, CPU us per datagram): send() 1.75, sendmmsg x20 1.67, GSO x10
    //    0.54, GSO x20 0.46. The kernel's per-datagram stack work dominates, not
    //    syscall entry, so sendmmsg barely helps and GSO does. (An earlier note
    //    here said batching of any kind gave only +7-12%; that was probably measured
    //    under WSL.)
    //
    //  * UdpSocket::send()/recv() are hand-written futures: the first poll() tries
    //    a non-blocking syscall on the calling thread (no allocation, no thread
    //    hop) and only falls back to a libuv-thread hop on EAGAIN. Loopback never
    //    hits the send slow path, but a real NIC can once the kernel send buffer
    //    fills -- measure the fast/slow ratio then. On the recv side (the future
    //    VITA-49 reader) the fast path only hits when a datagram is already
    //    queued, so a consumer that keeps up will take the slow path often.
    // With `sim --render-threads` above 1 the render threads are an OpenMP team (see common/tasksys.cpp), which
    // by libgomp's default spins for a while after each piece of work: whole cores burned whenever the
    // generator is ahead of real time and waiting. Default to passive, as the adsb decoder does: libgomp reads
    // its env only at load, before main(), so set it and re-execute once; an explicit OMP_WAIT_POLICY or
    // GOMP_SPINCOUNT wins. If exec fails we just run with the default policy.
    if (std::getenv("OMP_WAIT_POLICY") == nullptr && std::getenv("GOMP_SPINCOUNT") == nullptr) {
        setenv("OMP_WAIT_POLICY", "passive", 1);
        execv("/proc/self/exe", argv);
    }

    // Every thread here paces or hands off packets, so all get a short slice: woken on time, it preempts
    // whatever else holds its core instead of waiting out that thread's slice and then bursting.
    adsb::cpu_layout::set_short_slice(true);
    return coro::Runtime().block_on(async_main(argc, argv));
}
