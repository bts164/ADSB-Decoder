// Phase-0 CLI: reads IQ from a file, filter coefficients from a file, runs
// the ispc-vectorized demodulator, prints decoded frames. See README.md
// "Implementation phasing" for what this is and isn't (no shared library,
// no SDR plugins yet — that comes later).
//
// This file is just argument parsing and picking which coro::CoroStream<IqBlock>
// source to build (file_stream.h for file replay, rtlsdr_stream.h for
// --rtlsdr live streaming) -- both modes hand their stream to pipeline.h's
// run_demod_loop the same way. See pipeline.h/frame_decode.h/aircraft.h for
// frame decoding, ws_publisher.h for the WebSocket fan-out, and spectrum.h
// for the waterfall FFT side channel.

#include <cstdint>
#include <iostream>
#include <string>

#include <argparse/argparse.hpp>

#include <coro/coro.h>
#include <coro/runtime/runtime.h>

#include "aircraft_history.h"
#include "file_stream.h"
#include "filter_bank.h"
#include "pipeline.h"
#include "rtlsdr_stream.h"
#include "sigmf_meta.h"
#include "ws_publisher.h"

coro::Coro<int> async_main(int argc, char** argv) {
    argparse::ArgumentParser program("adsb");
    program.add_argument("--iq").default_value(std::string{}).help(
        "input SigMF IQ data file (.sigmf-data); required unless --rtlsdr is given");
    program.add_argument("--filter").required().help("filter bank coefficients file (.bin)");
    program.add_argument("--filter-meta").required().help("filter bank metadata file (.meta)");
    program.add_argument("--rate").scan<'g', double>().default_value(0.0).help(
        "sample rate in Hz (file mode default: read from --meta / <iq>'s .sigmf-meta sibling; "
        "--rtlsdr mode default: 2.4e6)");
    program.add_argument("--meta").default_value(std::string{}).help(
        "SigMF metadata file (.sigmf-meta); defaults to <iq>'s .sigmf-meta sibling");
    program.add_argument("--preamble-min").scan<'g', float>().default_value(3.0f).help(
        "preamble correlation score threshold");
    program.add_argument("--slice-mag-min").scan<'g', float>().default_value(2.0f * 56.0f).help(
        "bit-slice magnitude threshold");
    program.add_argument("--rtlsdr").flag().help(
        "stream and demodulate live from an RTL-SDR device instead of reading --iq");
    program.add_argument("--device").scan<'d', int>().default_value(0).help(
        "RTL-SDR device index (--rtlsdr mode)");
    program.add_argument("--freq").scan<'g', double>().default_value(1090000000.0).help(
        "center frequency in Hz (--rtlsdr mode; default: 1090 MHz)");
    program.add_argument("--gain").scan<'g', double>().default_value(-1.0).help(
        "tuner gain in dB, or negative for auto gain (--rtlsdr mode)");
    program.add_argument("--rtlsdr-buf-num").scan<'u', unsigned>().default_value(0u).help(
        "number of librtlsdr async transfer buffers in flight (--rtlsdr mode; 0 = librtlsdr default: 15)");
    program.add_argument("--rtlsdr-buf-len").scan<'u', unsigned>().default_value(0u).help(
        "librtlsdr async transfer buffer length in bytes, multiple of 512 (--rtlsdr mode; "
        "0 = librtlsdr default: 16*32*512 = 262144 bytes, i.e. 131072 IQ sample pairs, "
        "~54.6 ms at 2.4 Msps)");
    program.add_argument("--ws-port").scan<'u', unsigned>().default_value(0u).help(
        "publish decoded frames as JSON over a WebSocket server on this port (0 = disabled)");
    program.add_argument("--history-db").default_value(std::string{}).help(
        "persist aircraft position history to this SQLite file, updated live as frames decode, and "
        "backfilled to newly-connected --ws-port clients (empty = disabled)");
    program.add_argument("--no-stdout").flag().help(
        "suppress the per-frame stdout print (useful with --ws-port to publish only)");
    program.add_argument("--spectrum").flag().help(
        "compute and publish a periodic FFT of the raw pre-resample IQ over --ws-port, for a UI "
        "waterfall (--rtlsdr mode; off by default -- the FFT is wasted work when nothing's "
        "listening for it)");

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << err.what() << "\n" << program;
        co_return 1;
    }

    std::string iq_path = program.get<std::string>("--iq");
    std::string filter_bin_path = program.get<std::string>("--filter");
    std::string filter_meta_path = program.get<std::string>("--filter-meta");
    std::string sigmf_meta_path = program.get<std::string>("--meta");
    double rate_hz = program.get<double>("--rate");
    float preamble_min = program.get<float>("--preamble-min");
    float slice_mag_min = program.get<float>("--slice-mag-min");
    bool use_rtlsdr = program.get<bool>("--rtlsdr");
    unsigned ws_port = program.get<unsigned>("--ws-port");
    std::string history_db_path = program.get<std::string>("--history-db");
    bool print_stdout = !program.get<bool>("--no-stdout");
    bool enable_spectrum = program.get<bool>("--spectrum");

    if (ws_port != 0) ws_publisher_init(static_cast<uint16_t>(ws_port));
    if (!history_db_path.empty()) aircraft_history_init(history_db_path);

    if (use_rtlsdr) {
        if (rate_hz == 0.0) rate_hz = 2.4e6;
    } else {
        if (iq_path.empty()) {
            std::cerr << "--iq is required unless --rtlsdr is given\n" << program;
            co_return 1;
        }
        if (rate_hz == 0.0) {
            std::string meta_path = sigmf_meta_path.empty() ? iq_path + "-meta" : sigmf_meta_path;
            // .sigmf-data -> .sigmf-meta convention
            if (sigmf_meta_path.empty()) {
                auto pos = iq_path.rfind(".sigmf-data");
                if (pos != std::string::npos) meta_path = iq_path.substr(0, pos) + ".sigmf-meta";
            }
            if (auto r = read_sigmf_sample_rate(meta_path)) {
                rate_hz = *r;
            } else {
                std::cerr << "could not determine sample rate from " << meta_path << "; pass --rate <hz>\n";
                co_return 1;
            }
        }
    }

    FilterBank filter;
    try {
        filter = load_filter_bank(filter_bin_path, filter_meta_path);
    } catch (const std::exception& e) {
        std::cerr << "failed to load filter bank: " << e.what() << "\n";
        co_return 1;
    }

    // Only the IqBlock source differs between modes -- both hand their
    // coro::CoroStream<IqBlock> to the exact same run_demod_loop call below.
    double freq_hz = program.get<double>("--freq");
    auto stream = use_rtlsdr
        ? rtlsdr_iq_stream(program.get<int>("--device"), rate_hz, freq_hz, program.get<double>("--gain"),
                            filter.taps, program.get<unsigned>("--rtlsdr-buf-num"),
                            program.get<unsigned>("--rtlsdr-buf-len"))
        : file_iq_stream(iq_path, filter.taps, kFileStreamChunkSamples);

    uint64_t frame_count;
    try {
        frame_count = co_await coro::spawn_blocking(
            [filter = std::move(filter), rate_hz, preamble_min, slice_mag_min, stream = std::move(stream),
             print_stdout, freq_hz, enable_spectrum]() mutable {
                return run_demod_loop(std::move(filter), rate_hz, preamble_min, slice_mag_min, std::move(stream),
                                       print_stdout, freq_hz, enable_spectrum);
            });
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        co_return 1;
    }
    std::cerr << frame_count << " frame(s) decoded\n";

    co_return 0;
}

int main(int argc, char *argv[])
{
    return coro::Runtime().block_on(async_main(argc, argv));
}
