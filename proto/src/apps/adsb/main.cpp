// Phase-0 CLI: reads IQ from a file, a VITA-49 stream or an RTL-SDR, designs
// (or loads) the resampling filter for its sample rate, runs the
// ispc-vectorized demodulator, prints decoded frames. See README.md
// "Implementation phasing" for what this is and isn't (no shared library,
// no SDR plugins yet — that comes later).
//
// This file is just argument parsing and picking which coro::CoroStream<IqBlock>
// source to build, one argparse subcommand per source (`file`: file_stream.h,
// `vita49`: vita49_stream.h VRT/UDP streaming, `rtlsdr`: rtlsdr_stream.h) --
// every source hands its stream to pipeline.h's run_demod_loop the same way.
// See pipeline.h/frame_decode.h/aircraft.h for frame decoding, ws_publisher.h
// for the WebSocket fan-out, and spectrum.h for the waterfall FFT side channel.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include <argparse/argparse.hpp>

#include <coro/coro.h>
#include <coro/runtime/runtime.h>

#include "output/aircraft_history.h"
#include "input/file_stream.h"
#include "dsp/filter_bank.h"
#include "apps/adsb/pipeline.h"
#include "apps/filter_options.h"
#include "input/rtlsdr_stream.h"
#include "common/cpu_layout.h"
#include "input/sigmf_meta.h"
#include "input/vita49_stream.h"
#include "output/ws_publisher.h"

namespace {

coro::Coro<int> async_main(int argc, char** argv) {
    // Options every source shares are global and go before the subcommand; each source's own options belong to its
    // subcommand and go after it:  adsb --filter-taps 32 file --iq capture.sigmf-data
    argparse::ArgumentParser program("adsb");
    program.add_description("Demodulates ADS-B from IQ samples read from a SigMF file, received as a VITA-49 (VRT) "
                            "stream, or streamed from a live RTL-SDR. Choose the source with a subcommand: `file`, "
                            "`vita49` or `rtlsdr` (`adsb <subcommand> --help` lists that source's options).");
    add_filter_options(program);
    program.add_argument("--rate").scan<'g', double>().default_value(0.0).help(
        "sample rate in Hz (default: file source -- read from the SigMF metadata; vita49 and rtlsdr sources -- "
        "2.4e6; a vita49 stream whose IF Context packets advertise a different rate is rejected)");
    program.add_argument("--freq").scan<'g', double>().default_value(1090000000.0).help(
        "center frequency in Hz, used to label published spectrum frames (default 1090 MHz; rtlsdr source: also "
        "the tuner frequency)");
    program.add_argument("--preamble-min").scan<'g', float>().default_value(3.0f).help(
        "preamble correlation score threshold");
    program.add_argument("--slice-mag-min").scan<'g', float>().default_value(2.0f * 56.0f).help(
        "bit-slice magnitude threshold");
    program.add_argument("--ws-port").scan<'u', unsigned>().default_value(0u).help(
        "publish decoded frames as JSON over a WebSocket server on this port (0 = disabled)");
    program.add_argument("--history-db").default_value(std::string{}).help(
        "record every aircraft update to this SQLite file, live, for the UI's time scrubber; --ws-port "
        "clients get the recent sky on connect and can seek through it (empty = disabled)");
    program.add_argument("--no-stdout").flag().help(
        "suppress the per-frame stdout print (useful with --ws-port to publish only)");
    program.add_argument("--debug-h5").default_value(std::string{}).help(
        "write every decoded frame, with its IQ, envelope and the filter and parameters to rerun the demodulator "
        "offline, to this HDF5 file (see README.md \"Debug HDF5\")");
    program.add_argument("--debug-h5-failed-only").flag().help("with --debug-h5, record only frames that fail CRC");
    program.add_argument("--spectrum").flag().help(
        "compute and publish a periodic FFT of the raw pre-resample IQ over --ws-port, for a UI "
        "waterfall (off by default -- the FFT is wasted work when nothing's listening for it)");

    argparse::ArgumentParser file_cmd("file");
    file_cmd.add_description("Replay a SigMF recording.");
    file_cmd.add_argument("--iq").required().help("input SigMF IQ data file (.sigmf-data)");
    file_cmd.add_argument("--meta").default_value(std::string{}).help(
        "SigMF metadata file (.sigmf-meta) to read the sample rate from; defaults to --iq's .sigmf-meta sibling");

    argparse::ArgumentParser vita49_cmd("vita49");
    vita49_cmd.add_description("Receive a live VITA-49/VRT IQ stream over UDP (as sent by vita49_send).");
    vita49_cmd.add_argument("--port").scan<'u', unsigned>().required().help("UDP port to receive VRT packets on");
    vita49_cmd.add_argument("--bind").default_value(std::string("0.0.0.0")).help("address to bind");
    vita49_cmd.add_argument("--stream-id").scan<'u', unsigned>().help(
        "only accept this VRT stream id (default: the first one seen)");
    vita49_cmd.add_argument("--idle-timeout").scan<'g', double>().default_value(2.0).help(
        "stop after this many seconds without packets, once the first has arrived (0 = never)");

    argparse::ArgumentParser rtl_cmd("rtlsdr");
    rtl_cmd.add_description("Stream from a live RTL-SDR.");
    rtl_cmd.add_argument("--device").scan<'d', int>().default_value(0).help("RTL-SDR device index");
    rtl_cmd.add_argument("--gain").scan<'g', double>().default_value(-1.0).help(
        "tuner gain in dB, or negative for auto gain");
    rtl_cmd.add_argument("--buf-num").scan<'u', unsigned>().default_value(0u).help(
        "number of librtlsdr async transfer buffers in flight (0 = librtlsdr default: 15)");
    rtl_cmd.add_argument("--buf-len").scan<'u', unsigned>().default_value(0u).help(
        "librtlsdr async transfer buffer length in bytes, multiple of 512 (0 = librtlsdr default: "
        "16*32*512 = 262144 bytes, i.e. 131072 IQ sample pairs, ~54.6 ms at 2.4 Msps)");

    program.add_subparser(file_cmd);
    program.add_subparser(vita49_cmd);
    program.add_subparser(rtl_cmd);

    try {
        program.parse_args(argc, argv);
        if (!program.is_subcommand_used(file_cmd) && !program.is_subcommand_used(vita49_cmd) &&
            !program.is_subcommand_used(rtl_cmd)) {
            throw std::runtime_error("a source subcommand is required: file, vita49 or rtlsdr");
        }
    } catch (const std::exception& err) {
        // Show the usage of the parser the bad argument belonged to.
        std::cerr << err.what() << "\n";
        if (program.is_subcommand_used(file_cmd)) {
            std::cerr << file_cmd;
        } else if (program.is_subcommand_used(vita49_cmd)) {
            std::cerr << vita49_cmd;
        } else if (program.is_subcommand_used(rtl_cmd)) {
            std::cerr << rtl_cmd;
        } else {
            std::cerr << program;
        }
        co_return 1;
    }

    double rate_hz = program.get<double>("--rate");
    double freq_hz = program.get<double>("--freq");
    float preamble_min = program.get<float>("--preamble-min");
    float slice_mag_min = program.get<float>("--slice-mag-min");
    unsigned ws_port = program.get<unsigned>("--ws-port");
    std::string history_db_path = program.get<std::string>("--history-db");
    bool print_stdout = !program.get<bool>("--no-stdout");
    bool enable_spectrum = program.get<bool>("--spectrum");
    DebugRecordOptions debug{
        .path = program.get<std::string>("--debug-h5"),
        .failed_only = program.get<bool>("--debug-h5-failed-only"),
        .command_line = {},
    };
    for (int i = 0; i < argc; i++) debug.command_line += (i ? " " : "") + std::string(argv[i]);

    if (ws_port != 0) ws_publisher_init(static_cast<uint16_t>(ws_port));
    if (!history_db_path.empty()) aircraft_history_init(history_db_path);

    if (program.is_subcommand_used(rtl_cmd) || program.is_subcommand_used(vita49_cmd)) {
        if (rate_hz == 0.0) rate_hz = 2.4e6;
    } else if (program.is_subcommand_used(file_cmd) && rate_hz == 0.0) {
        const std::string iq_path = file_cmd.get<std::string>("--iq");
        std::string meta_path = file_cmd.get<std::string>("--meta");
        if (meta_path.empty()) {
            // .sigmf-data -> .sigmf-meta convention
            auto pos = iq_path.rfind(".sigmf-data");
            meta_path = pos != std::string::npos ? iq_path.substr(0, pos) + ".sigmf-meta" : iq_path + "-meta";
        }
        if (auto r = read_sigmf_sample_rate(meta_path)) {
            rate_hz = *r;
        } else {
            std::cerr << "could not determine sample rate from " << meta_path << "; pass --rate <hz>\n";
            co_return 1;
        }
    }

    FilterBank filter;
    try {
        filter = make_filter(program, rate_hz);
        std::cerr << "filter: " << filter.taps << " taps x " << filter.num_phases << " phases, " << filter.description
                  << "\n";
    } catch (const std::exception& e) {
        std::cerr << "filter bank: " << e.what() << "\n";
        co_return 1;
    }
    const IqPadding pad = AdsbDemod::padding(filter, rate_hz);

    // Only the IqBlock source differs between modes -- all of them hand their
    // coro::CoroStream<IqBlock> to the exact same run_demod_loop call below.
    std::optional<coro::CoroStream<IqBlock>> stream;
    if (program.is_subcommand_used(vita49_cmd)) {
        stream.emplace(vita49_iq_stream(
            {
                .bind_host = vita49_cmd.get<std::string>("--bind"),
                .port = static_cast<uint16_t>(vita49_cmd.get<unsigned>("--port")),
                .sample_rate_hz = rate_hz,
                .stream_id = vita49_cmd.present<unsigned>("--stream-id"),
                .idle_timeout_s = vita49_cmd.get<double>("--idle-timeout"),
            },
            pad, kVita49StreamChunkSamples));
    } else if (program.is_subcommand_used(rtl_cmd)) {
        stream.emplace(rtlsdr_iq_stream(rtl_cmd.get<int>("--device"), rate_hz, freq_hz, rtl_cmd.get<double>("--gain"),
                                        pad, rtl_cmd.get<unsigned>("--buf-num"), rtl_cmd.get<unsigned>("--buf-len")));
    } else {
        stream.emplace(file_iq_stream(file_cmd.get<std::string>("--iq"), pad, kFileStreamChunkSamples));
    }

    uint64_t frame_count;
    try {
        frame_count = co_await coro::spawn_blocking(
            [filter = std::move(filter), rate_hz, preamble_min, slice_mag_min, stream = std::move(*stream),
             print_stdout, freq_hz, enable_spectrum, debug = std::move(debug)]() mutable {
                adsb::cpu_layout::bind_compute_thread();
                return run_demod_loop(std::move(filter), rate_hz, preamble_min, slice_mag_min, std::move(stream),
                                       print_stdout, freq_hz, enable_spectrum, debug);
            });
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        co_return 1;
    }
    std::cerr << frame_count << " frame(s) decoded\n";

    co_return 0;
}

}  // namespace

int main(int argc, char *argv[])
{
    // libgomp's default wait policy spins idle workers for ~300k iterations after each launch (see tasksys.cpp):
    // free when chunks arrive back to back (file mode), but live sources deliver a chunk every ~55 ms, so the
    // workers spun through most of the gap -- ~5 cores busy vs ~0.3 passive. libgomp reads its env only at load,
    // before main(), so default to passive by setting it and re-executing once; an explicit OMP_WAIT_POLICY or
    // GOMP_SPINCOUNT wins. If exec fails we just run with the default policy.
    if (std::getenv("OMP_WAIT_POLICY") == nullptr && std::getenv("GOMP_SPINCOUNT") == nullptr) {
        setenv("OMP_WAIT_POLICY", "passive", 1);
        execv("/proc/self/exe", argv);
    }

    if (adsb::cpu_layout::enabled()) {
        adsb::cpu_layout::bind_async_thread();
        return coro::Runtime(1).block_on(async_main(argc, argv));
    }
    return coro::Runtime().block_on(async_main(argc, argv));
}
