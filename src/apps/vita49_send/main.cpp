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
// packet_sender.* sends whichever stream it is given. The packets come from
// src/vrt/encode.h and go out in src/net/'s GSO batches.

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <absl/strings/str_format.h>
#include <argparse/argparse.hpp>

#include "apps/log_options.h"
#include "apps/version_options.h"
#include "apps/vita49_send/packet_sender.h"
#include "apps/vita49_send/rtlsdr_source.h"
#include "apps/vita49_send/sim_source.h"
#include "common/version_info.h"
#include "net/gso.h"
#include "vrt/encode.h"

#include <coro/coro.h>
#include <coro/coro_stream.h>
#include <coro/io/signal.h>
#include <coro/io/socket_address.h>
#include <coro/io/udp_socket.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/select.h>
#include <coro/sync/watch.h>
#include <coro/task/spawn_builder.h>

namespace {

void add_options(argparse::ArgumentParser& program) {
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
    add_log_options(program, "2 adds a line for every send, sim render step and drop");
    VersionOptions::add_options(program);
}

// Parses the command line and starts logging. On an error prints it, with the usage of the parser the bad
// argument belonged to, and returns false. With --version prints the report and exits.
bool parse_command_line(argparse::ArgumentParser& program, const argparse::ArgumentParser& rtl_cmd,
                        const argparse::ArgumentParser& sim_cmd, int argc, char** argv) {
    try {
        program.parse_args(argc, argv);
        VersionOptions::exit_if_given(program, "vita49_send");
        if (!program.is_subcommand_used(rtl_cmd) && !program.is_subcommand_used(sim_cmd)) {
            throw std::runtime_error("a source subcommand is required: rtlsdr or sim");
        }
    } catch (const std::exception& err) {
        VersionOptions::exit_if_given(program, "vita49_send");
        std::cerr << err.what() << "\n";
        if (program.is_subcommand_used(rtl_cmd)) {
            std::cerr << rtl_cmd;
        } else if (program.is_subcommand_used(sim_cmd)) {
            std::cerr << sim_cmd;
        } else {
            std::cerr << program;
        }
        return false;
    }
    try {
        init_logging(program);
    } catch (const std::exception& err) {
        std::cerr << err.what() << "\n";
        return false;
    }
    return true;
}

coro::Coro<int> async_main(int argc, char** argv) {
    // Options every source shares go before the subcommand, each source's own after it:
    //   vita49_send --dest-port 4991 sim --scenario boston.json
    argparse::ArgumentParser program("vita49_send", "", argparse::default_arguments::help);
    argparse::ArgumentParser rtl_cmd("rtlsdr");
    argparse::ArgumentParser sim_cmd("sim");
    add_options(program);
    RtlSdrSourceParams::add_options(rtl_cmd);
    SimStreamParams::add_options(sim_cmd);
    program.add_subparser(rtl_cmd);
    program.add_subparser(sim_cmd);
    if (!parse_command_line(program, rtl_cmd, sim_cmd, argc, argv)) co_return 1;
    LOGF(INFO, "vita49_send %s", adsb::version_info::summary_line());

    const double rate_hz = program.get<double>("--rate");
    const double freq_hz = program.get<double>("--freq");
    const double context_interval_s = program.get<double>("--context-interval");
    const std::string dest_host = program.get<std::string>("--dest-host");
    const auto dest_port = static_cast<uint16_t>(program.get<unsigned>("--dest-port"));
    const uint32_t stream_id = program.get<unsigned>("--stream-id");
    const size_t samples_per_packet = program.get<unsigned>("--samples-per-packet");

    const auto dest = coro::SocketAddress::parse(dest_host, dest_port);
    if (!dest) {
        LOGF(ERROR, "invalid --dest-host: %s", dest_host);
        co_return 1;
    }
    auto sock = co_await coro::UdpSocket::bind("0.0.0.0", 0);
    co_await sock.connect(*dest);
    LOGF(INFO, "streaming VRT/UDP to %s: stream id %u, %.3f Msps, %.3f MHz, %u samples per packet (%u-byte "
         "datagrams), %s",
         dest->to_string(), stream_id, rate_hz / 1e6, freq_hz / 1e6, samples_per_packet,
         vrt::data_packet_bytes(samples_per_packet),
         context_interval_s > 0 ? absl::StrFormat("context every %g s", context_interval_s) : "no context packets");

    // The sources report what they drop on the pace statistics.
    auto [pace_tx, pace_rx] = coro::watch_channel(PaceStats{});
    try {
        // Only the source differs between subcommands: send_loop() sends whichever stream it is given.
        std::optional<coro::CoroStream<PacketBatch>> stream;
        double duration_s = 0;  // the sim's --duration; 0 = until Ctrl+C or the source ends
        if (program.is_subcommand_used(sim_cmd)) {
            SimStreamParams params = SimStreamParams::from_options(program, sim_cmd);
            duration_s = params.duration_s;
            // At its rates (up to 200 Msps) the sim needs GSO: up to 44 data packets of the default size per send.
            const size_t batch_packets = net::enable_gso(sock, vrt::data_packet_bytes(samples_per_packet));
            stream.emplace(sim_packet_stream(std::move(params), batch_packets, pace_tx.clone()));
        } else {
            // RtlSdr logs the device settings and transfer buffers in effect.
            stream.emplace(rtlsdr_packet_stream(RtlSdrSourceParams::from_options(program, rtl_cmd), pace_tx.clone()));
        }
        // The one place Ctrl+C is handled: select() drops the send task's handle, which cancels send_loop() and
        // waits for it, and that drops the source stream with it. Neither source watches for the signal itself.
        // Dropping the sim stream cancels its render task (see sim_generate()); dropping the rtlsdr stream
        // destroys its RtlSdr, which stops the read. A task of its own so that a profile shows its polls by
        // name: each includes the time in the kernel's send path.
        auto done = co_await coro::select(
            coro::build_task().name("udp_send").spawn(
                send_loop(std::move(sock), std::move(*stream), rate_hz, std::move(pace_tx), std::move(pace_rx))),
            coro::signal(SIGINT));
        if (done.index() == 1) {
            LOGF(INFO, "stopped by SIGINT");
        } else if (duration_s > 0) {
            LOGF(INFO, "stopped: reached --duration (%g s)", duration_s);
        } else {
            LOGF(INFO, "stopped: the source ended");
        }
    } catch (const std::exception& err) {  // a bad sim option, the device couldn't be opened, or a send failed
        LOGF(ERROR, "%s", err.what());
        co_return 1;
    }
    co_return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    coro::Runtime rt;
    LOGF(INFO, "vita49_send starting on %zu cores", rt.worker_count());
    return rt.block_on(async_main(argc, argv));
}
