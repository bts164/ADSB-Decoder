// Standalone CLI: streams IQ out as VRT (VITA-49) IF Data packets over UDP, from any of adsb's sources (a SigMF
// file, an RTL-SDR, a Sidekiq, a UHD radio or a VRT stream) or from simulated ADS-B -- a test source playing the
// role a real VITA-49-capable SDR front-end would, feeding `adsb vita49` (input/vita49_stream.h) or a
// third-party receiver. See doc/design/vita49-format.md for the wire format this implements (IF Data packets
// plus periodic IF Context packets carrying bandwidth, RF reference frequency, sample rate and payload format;
// no Class ID, no Trailer), and doc/design/source_sink.md for the pipeline.
//
// This is deliberately its own executable, not folded into `adsb` -- it has nothing to do with demodulation,
// and its sink (UDP) is different from adsb's own IqBlock-consuming pipeline.
//
// Files: this one parses the command line and builds the chain for the source. adsb's sources (src/input/)
// go through stitch() and packetize.*, which resamples their blocks into packets, and pace.* for a file;
// sim_source.* renders straight into packets. Either way packet_sender.* sends the coro::CoroStream<PacketBatch>
// it is given. The packets come from src/vrt/encode.h and go out in src/net/'s GSO batches.

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
#include "apps/vita49_send/pace.h"
#include "apps/vita49_send/packet_sender.h"
#include "apps/vita49_send/packetize.h"
#include "apps/vita49_send/sim_source.h"
#include "common/version_info.h"
#include "dsp/iq_block.h"
#include "dsp/iq_resampler.h"
#include "input/file_stream.h"
#include "input/rtlsdr_stream.h"
#ifdef ADSB_WITH_SIDEKIQ
#include "input/sidekiq_stream.h"
#endif
#ifdef ADSB_WITH_UHD
#include "input/uhd_stream.h"
#endif
#include "input/vita49_stream.h"
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

// The parsers, one per subcommand.
struct Commands {
    argparse::ArgumentParser file{"file"};
    argparse::ArgumentParser vita49{"vita49"};
    argparse::ArgumentParser rtlsdr{"rtlsdr"};
#ifdef ADSB_WITH_SIDEKIQ
    argparse::ArgumentParser sidekiq{"sidekiq"};
#endif
#ifdef ADSB_WITH_UHD
    argparse::ArgumentParser uhd{"uhd"};
#endif
    argparse::ArgumentParser sim{"sim"};
};

std::string source_names() {
    std::string names = "file, vita49, rtlsdr";
#ifdef ADSB_WITH_SIDEKIQ
    names += ", sidekiq";
#endif
#ifdef ADSB_WITH_UHD
    names += ", uhd";
#endif
    return names + " or sim";
}

void add_options(argparse::ArgumentParser& program, Commands& cmds) {
    program.add_description("Streams IQ samples as VITA-49 (VRT) IF Data packets over UDP, from any of adsb's "
                            "sources or from simulated ADS-B traffic. Choose the source with a subcommand: " +
                            source_names() + " (`vita49_send <subcommand> --help` lists that source's options).");
    program.add_argument("--context-interval").scan<'g', double>().default_value(1.0).help(
        "seconds of signal between IF Context packets (sample rate, RF frequency, bandwidth, payload format); "
        "one is always sent before the first data packet. 0 = never send any");
    program.add_argument("--dest-host").default_value(std::string("127.0.0.1")).help(
        "destination host to stream VRT/UDP packets to");
    program.add_argument("--dest-port").scan<'u', unsigned>().required().help(
        "destination UDP port to stream VRT packets to");
    program.add_argument("--stream-id").scan<'u', unsigned>().default_value(1u).help(
        "VRT Stream ID to tag every packet with");
    StreamOptions::add_options(program);
    add_log_options(program, "2 adds a line for every send, sim render step and drop");
    VersionOptions::add_options(program);

    FileStreamParams::add_options(cmds.file);
    Vita49StreamParams::add_options(cmds.vita49);
    RtlSdrStreamParams::add_options(cmds.rtlsdr);
    for (argparse::ArgumentParser* cmd : {&cmds.file, &cmds.vita49, &cmds.rtlsdr}) program.add_subparser(*cmd);
#ifdef ADSB_WITH_SIDEKIQ
    SidekiqStreamParams::add_options(cmds.sidekiq);
    program.add_subparser(cmds.sidekiq);
#endif
#ifdef ADSB_WITH_UHD
    UhdStreamParams::add_options(cmds.uhd);
    program.add_subparser(cmds.uhd);
#endif
    SimStreamParams::add_options(cmds.sim);
    program.add_subparser(cmds.sim);
}

// The subcommand used, or nullptr.
const argparse::ArgumentParser* used_command(const argparse::ArgumentParser& program, const Commands& cmds) {
    for (const argparse::ArgumentParser* cmd : {&cmds.file, &cmds.vita49, &cmds.rtlsdr, &cmds.sim})
        if (program.is_subcommand_used(*cmd)) return cmd;
#ifdef ADSB_WITH_SIDEKIQ
    if (program.is_subcommand_used(cmds.sidekiq)) return &cmds.sidekiq;
#endif
#ifdef ADSB_WITH_UHD
    if (program.is_subcommand_used(cmds.uhd)) return &cmds.uhd;
#endif
    return nullptr;
}

// Parses the command line and starts logging. On an error prints it, with the usage of the parser the bad
// argument belonged to, and returns false. With --version prints the report and exits.
bool parse_command_line(argparse::ArgumentParser& program, const Commands& cmds, int argc, char** argv) {
    try {
        program.parse_args(argc, argv);
        VersionOptions::exit_if_given(program, "vita49_send");
        if (!used_command(program, cmds))
            throw std::runtime_error("a source subcommand is required: " + source_names());
        if (program.is_subcommand_used(cmds.sim) && StreamOptions::resampling_options_used(program))
            throw std::runtime_error("the sim source takes no --stream-rate, filter options or --stream-format "
                                     "other than i16be: it renders int16 at its own --rate");
    } catch (const std::exception& err) {
        VersionOptions::exit_if_given(program, "vita49_send");
        std::cerr << err.what() << "\n";
        if (const argparse::ArgumentParser* cmd = used_command(program, cmds)) {
            std::cerr << *cmd;
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
    Commands cmds;
    add_options(program, cmds);
    if (!parse_command_line(program, cmds, argc, argv)) co_return 1;
    LOGF(INFO, "vita49_send %s", adsb::version_info::summary_line());

    const double context_interval_s = program.get<double>("--context-interval");
    const std::string dest_host = program.get<std::string>("--dest-host");
    const auto dest_port = static_cast<uint16_t>(program.get<unsigned>("--dest-port"));
    const uint32_t stream_id = program.get<unsigned>("--stream-id");

    const auto dest = coro::SocketAddress::parse(dest_host, dest_port);
    if (!dest) {
        LOGF(ERROR, "invalid --dest-host: %s", dest_host);
        co_return 1;
    }
    auto sock = co_await coro::UdpSocket::bind("0.0.0.0", 0);
    co_await sock.connect(*dest);

    // The stages report what they drop on the pace statistics.
    auto [pace_tx, pace_rx] = coro::watch_channel(PaceStats{});
    try {
        // Only the chain before send_loop() differs between subcommands: send_loop() sends whichever stream it is
        // given.
        std::optional<coro::CoroStream<PacketBatch>> stream;
        double rate_hz = 0;  // the stream's
        double freq_hz = 0;
        size_t samples_per_packet = 0;
        vrt::PayloadFormat payload = vrt::PayloadFormat::i16;
        bool payload_format = true;
        double duration_s = 0;  // the sim's --duration; 0 = until Ctrl+C or the source ends
        if (program.is_subcommand_used(cmds.sim)) {
            SimStreamParams params = SimStreamParams::from_options(program, cmds.sim);
            rate_hz = params.sim.sample_rate_hz;
            freq_hz = params.rf_freq_hz;
            samples_per_packet = params.samples_per_packet;
            payload_format = params.payload_format;
            duration_s = params.duration_s;
            // At its rates (up to 200 Msps) the sim needs GSO: up to 44 data packets of the default size per send.
            const size_t batch_packets = net::enable_gso(sock, vrt::data_packet_bytes(samples_per_packet, payload));
            stream.emplace(sim_packet_stream(std::move(params), batch_packets, pace_tx.clone()));
        } else {
            const StreamOptions stream_options = StreamOptions::from_options(program);
            // The resampler is built for the source's rate, and the source pads its blocks for it.
            std::optional<IqResampler> resampler;
            std::optional<coro::CoroStream<IqBuffer>> source;
            if (program.is_subcommand_used(cmds.vita49)) {
                Vita49StreamParams params = Vita49StreamParams::from_options(program, cmds.vita49);
                freq_hz = params.freq_hz;
                resampler.emplace(stream_options.make_resampler(params.receiver.sample_rate_hz));
                source.emplace(vita49_iq_stream(std::move(params.receiver), resampler->padding(),
                                                Vita49StreamParams::kChunkSamples));
            } else if (program.is_subcommand_used(cmds.rtlsdr)) {
                RtlSdrStreamParams params = RtlSdrStreamParams::from_options(program, cmds.rtlsdr);
                freq_hz = params.tuning.freq_hz;
                resampler.emplace(stream_options.make_resampler(params.tuning.rate_hz));
                source.emplace(rtlsdr_iq_stream(std::move(params), resampler->padding()));
#ifdef ADSB_WITH_SIDEKIQ
            } else if (program.is_subcommand_used(cmds.sidekiq)) {
                SidekiqStreamParams params = SidekiqStreamParams::from_options(program, cmds.sidekiq);
                freq_hz = params.freq_hz;
                resampler.emplace(stream_options.make_resampler(params.rate_hz));
                source.emplace(sidekiq_iq_stream(std::move(params), resampler->padding(),
                                                 SidekiqStreamParams::kChunkSamples));
#endif
#ifdef ADSB_WITH_UHD
            } else if (program.is_subcommand_used(cmds.uhd)) {
                UhdStreamParams params = UhdStreamParams::from_options(program, cmds.uhd);
                freq_hz = params.freq_hz;
                resampler.emplace(stream_options.make_resampler(params.rate_hz));
                source.emplace(uhd_iq_stream(std::move(params), resampler->padding(), UhdStreamParams::kChunkSamples));
#endif
            } else {
                FileStreamParams params = FileStreamParams::from_options(program, cmds.file);
                freq_hz = params.freq_hz;
                resampler.emplace(stream_options.make_resampler(params.rate_hz));
                source.emplace(file_iq_stream(std::move(params.iq_path), params.format, resampler->padding(),
                                              FileStreamParams::kChunkSamples));
            }
            rate_hz = resampler->output_rate_hz();
            samples_per_packet = stream_options.samples_per_packet;
            payload = stream_options.payload();
            payload_format = stream_options.payload_format;
            const size_t batch_packets = net::enable_gso(sock, vrt::data_packet_bytes(samples_per_packet, payload));
            const PacketizeParams params{
                .stream_id = stream_id,
                .samples_per_packet = samples_per_packet,
                .batch_packets = batch_packets,
                .rf_freq_hz = freq_hz,
                .context_interval_s = context_interval_s,
                .payload_format = payload_format,
            };
            auto batches = packetize(stitch(std::move(*source)), params, std::move(*resampler), pace_tx.clone());
            // A file has no clock of its own: without pace() it would go as fast as the socket takes it.
            if (program.is_subcommand_used(cmds.file)) {
                LOGF(INFO, "pacing the file to real time at %.3f Msps", rate_hz / 1e6);
                stream.emplace(pace(std::move(batches), rate_hz, pace_tx.clone()));
            } else {
                stream.emplace(std::move(batches));
            }
        }
        LOGF(INFO, "streaming VRT/UDP to %s: stream id %u, %.3f Msps, %.3f MHz, %u samples per packet (%u-byte "
             "datagrams), %s%s",
             dest->to_string(), stream_id, rate_hz / 1e6, freq_hz / 1e6, samples_per_packet,
             vrt::data_packet_bytes(samples_per_packet, payload),
             context_interval_s > 0 ? absl::StrFormat("context every %g s", context_interval_s) : "no context packets",
             context_interval_s > 0 && !payload_format ? ", without the Data Payload Format field" : "");

        // The one place Ctrl+C is handled: select() drops the send task's handle, which cancels send_loop() and
        // waits for it, and that drops the chain before it. No source watches for the signal itself. Dropping
        // the sim stream cancels its render task (see sim_generate()); dropping the packetize() stream cancels
        // its task, which drops the source's. A task of its own so that a profile shows its polls by name: each
        // includes the time in the kernel's send path.
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
    } catch (const std::exception& err) {  // a bad option, a device that couldn't be opened, or a failed send
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
