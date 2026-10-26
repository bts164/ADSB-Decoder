// The adsb receiver CLI: reads IQ from a file, a VITA-49 stream or an RTL-SDR,
// designs (or loads) the resampling filter for its sample rate, runs the
// ispc-vectorized demodulator and publishes decoded frames and aircraft. See
// README.md "The receiver (adsb)".
//
// This file is just argument parsing and picking which coro::CoroStream<IqBuffer>
// source to build, one argparse subcommand per source (`file`: file_stream.h,
// `vita49`: vita49_stream.h VRT/UDP streaming, `rtlsdr`: rtlsdr_stream.h,
// `sidekiq`: sidekiq_stream.h, in a build with ADSB_WITH_SIDEKIQ, and `uhd`:
// uhd_stream.h, in a build with ADSB_WITH_UHD), each
// with its options beside it -- every source hands its stream to pipeline.h's
// run_demod_loop the same way.
// The `serve` subcommand has no source: it only serves an existing history
// database to WebSocket clients until Ctrl+C.
// See pipeline.h/frame_decode.h/aircraft.h for frame decoding, api_server.h
// for the UI's WebSocket API, and spectrum.h for the waterfall FFT side channel.

#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <argparse/argparse.hpp>

#include <coro/coro.h>
#include <coro/io/signal.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/select.h>
#include <coro/sync/watch.h>

#include "apps/adsb/pipeline.h"
#include "apps/filter_options.h"
#include "apps/log_options.h"
#include "apps/version_options.h"
#include "common/version_info.h"
#include "dsp/filter_bank.h"
#include "input/file_stream.h"
#include "input/rtlsdr_stream.h"
#ifdef ADSB_WITH_SIDEKIQ
#include "input/sidekiq_stream.h"
#endif
#ifdef ADSB_WITH_UHD
#include "input/uhd_stream.h"
#endif
#include "input/vita49_stream.h"
#include "output/aircraft_history.h"
#include "output/api_server.h"
#include "output/capture.h"

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
    argparse::ArgumentParser serve{"serve"};
};

// An InputStatus as the UI takes it; the UI warns when the input isn't keeping up. Here rather than in adsb_api,
// so that library doesn't need adsb_input.
api::InputStatus to_proto(const InputStatus& s) {
    api::InputStatus msg;
    msg.set_window_s(s.window_s);
    msg.set_rate_hz(s.rate_hz);
    msg.set_stream_s(s.stream_s);
    msg.set_lost_s(s.lost_s);
    msg.set_dropped_s(s.dropped_s);
    if (s.kernel_drops) msg.set_kernel_drops(*s.kernel_drops);
    msg.set_ok(s.ok());
    msg.set_summary(s.describe());
    return msg;
}

// Options every source shares are global and go before the subcommand; each source's own options belong to its
// subcommand and go after it:  adsb --filter-taps 32 file --iq capture.sigmf-data
void add_options(argparse::ArgumentParser& program, Commands& cmds) {
    program.add_description("Demodulates ADS-B from IQ samples read from a SigMF file, received as a VITA-49 (VRT) "
                            "stream, or streamed from a live RTL-SDR. Choose the source with a subcommand: `file`, "
                            "`vita49` or `rtlsdr` (`adsb <subcommand> --help` lists that source's options). `serve` "
                            "has no source and only serves an existing --history-db to --ws-port clients.");
    program.add_epilog("Environment variables, for profiling and tuning (each is logged when set):\n"
                       "  ADSB_TIMING=1                log the demodulator's stage times for every block, and the\n"
                       "                               demod loop's throughput every second (0: off)\n"
                       "  ADSB_NTASKS=N                split each block's kernels into N parts instead of one per\n"
                       "                               runtime worker");
    VersionOptions::add_options(program);
    FilterOptions::add_options(program);
    add_log_options(program,
                    "1 adds a line for each websocket call, 2 for each aircraft change, 3 for every decoded frame");
    DemodLoopParams::add_options(program);
    program.add_argument("--ws-port").scan<'u', uint16_t>().help(
        "serve the UI's API (doc/design/ui_api.md) over a WebSocket server on this port (default: no server)");
    program.add_argument("--ws-interval").scan<'g', double>().default_value(1.0).help(
        "seconds between a --ws-port client's live updates, unless its subscription asks otherwise");
    program.add_argument("--history-db").help(
        "record every aircraft update to this SQLite file, live, for the UI's time scrubber; --ws-port "
        "clients get the recent sky on connect and can seek through it (default: not recorded)");
    program.add_argument("--capture-dir").help(
        "with --ws-port and a source, start with the API's capture calls, which record the input IQ or the "
        "demodulator's envelope for a while as SigMF, writing into this directory; a client can change it or turn "
        "captures off and on (default: off until a client sets one)");

    FileStreamParams::add_options(cmds.file);
    Vita49StreamParams::add_options(cmds.vita49);
    RtlSdrStreamParams::add_options(cmds.rtlsdr);
#ifdef ADSB_WITH_SIDEKIQ
    SidekiqStreamParams::add_options(cmds.sidekiq);
    program.add_subparser(cmds.sidekiq);
#endif
#ifdef ADSB_WITH_UHD
    UhdStreamParams::add_options(cmds.uhd);
    program.add_subparser(cmds.uhd);
#endif
    cmds.serve.add_description("No input: serve an existing --history-db to --ws-port clients (the UI's history "
                               "views) until Ctrl+C.");
    for (argparse::ArgumentParser* cmd : {&cmds.file, &cmds.vita49, &cmds.rtlsdr, &cmds.serve})
        program.add_subparser(*cmd);
}

// The libraries loaded at run time that report their version, for --version. libsidekiq is the one --lib's
// default finds; one that can't be loaded is reported as such, and the rest still are.
std::vector<adsb::version_info::RuntimeLibrary> runtime_libraries() {
    std::vector<adsb::version_info::RuntimeLibrary> libraries;
#ifdef ADSB_WITH_UHD
    libraries.push_back({"uhd", uhd_library_version()});
#endif
#ifdef ADSB_WITH_SIDEKIQ
    try {
        libraries.push_back({"libsidekiq", sidekiq_library_version(SidekiqStreamParams::kDefaultLib)});
    } catch (const std::exception& err) {
        libraries.push_back({"libsidekiq", std::string("unavailable: ") + err.what()});
    }
#endif
    return libraries;
}

// Parses the command line and starts logging. On an error prints it, with the usage of the parser the bad
// argument belonged to, and returns false. With --version prints the report and exits.
bool parse_command_line(argparse::ArgumentParser& program, const Commands& cmds, int argc, char** argv) {
    try {
        program.parse_args(argc, argv);
        VersionOptions::exit_if_given(program, "adsb", runtime_libraries);
        if (!program.is_subcommand_used(cmds.file) && !program.is_subcommand_used(cmds.vita49) &&
            !program.is_subcommand_used(cmds.rtlsdr) && !program.is_subcommand_used(cmds.serve)
#ifdef ADSB_WITH_SIDEKIQ
            && !program.is_subcommand_used(cmds.sidekiq)
#endif
#ifdef ADSB_WITH_UHD
            && !program.is_subcommand_used(cmds.uhd)
#endif
        ) {
            std::string names = "file, vita49, rtlsdr";
#ifdef ADSB_WITH_SIDEKIQ
            names += ", sidekiq";
#endif
#ifdef ADSB_WITH_UHD
            names += ", uhd";
#endif
            throw std::runtime_error("a subcommand is required: " + names + " or serve");
        }
        if (program.present<uint16_t>("--ws-port") == uint16_t{0}) {
            throw std::runtime_error("--ws-port must be between 1 and 65535");
        }
        if (!(program.get<double>("--ws-interval") > 0.0)) {
            throw std::runtime_error("--ws-interval must be greater than 0");
        }
        if (program.is_subcommand_used(cmds.serve) &&
            (!program.present<uint16_t>("--ws-port") || !program.present("--history-db"))) {
            throw std::runtime_error("serve needs --ws-port and --history-db");
        }
        if (const auto dir = program.present("--capture-dir")) {
            if (!program.present<uint16_t>("--ws-port") || program.is_subcommand_used(cmds.serve))
                throw std::runtime_error("--capture-dir needs --ws-port and a source");
            if (auto resolved = resolve_capture_dir(*dir); !resolved)
                throw std::runtime_error("--capture-dir: " + resolved.error());
        }
        for (const argparse::ArgumentParser* cmd : {&cmds.file, &cmds.vita49, &cmds.rtlsdr}) {
            if (cmd->is_used("--rate") && !(cmd->get<double>("--rate") > 0.0))
                throw std::runtime_error("--rate must be greater than 0");
        }
#ifdef ADSB_WITH_SIDEKIQ
        if (program.is_subcommand_used(cmds.sidekiq)) {
            if (!(cmds.sidekiq.get<double>("--rate") > 0.0)) throw std::runtime_error("--rate must be greater than 0");
            if (cmds.sidekiq.is_used("--bandwidth") && !(cmds.sidekiq.get<double>("--bandwidth") > 0.0))
                throw std::runtime_error("--bandwidth must be greater than 0");
            (void)SidekiqStreamParams::from_options(program, cmds.sidekiq);  // --handle and --gain
        }
#endif
#ifdef ADSB_WITH_UHD
        if (program.is_subcommand_used(cmds.uhd)) {
            if (!(cmds.uhd.get<double>("--rate") > 0.0)) throw std::runtime_error("--rate must be greater than 0");
            if (cmds.uhd.is_used("--bandwidth") && !(cmds.uhd.get<double>("--bandwidth") > 0.0))
                throw std::runtime_error("--bandwidth must be greater than 0");
            (void)UhdStreamParams::from_options(program, cmds.uhd);  // --gain
        }
#endif
    } catch (const std::exception& err) {
        VersionOptions::exit_if_given(program, "adsb", runtime_libraries);
        std::cerr << err.what() << "\n";
        if (program.is_subcommand_used(cmds.file)) {
            std::cerr << cmds.file;
        } else if (program.is_subcommand_used(cmds.vita49)) {
            std::cerr << cmds.vita49;
        } else if (program.is_subcommand_used(cmds.rtlsdr)) {
            std::cerr << cmds.rtlsdr;
#ifdef ADSB_WITH_SIDEKIQ
        } else if (program.is_subcommand_used(cmds.sidekiq)) {
            std::cerr << cmds.sidekiq;
#endif
#ifdef ADSB_WITH_UHD
        } else if (program.is_subcommand_used(cmds.uhd)) {
            std::cerr << cmds.uhd;
#endif
        } else {
            std::cerr << program;
        }
        return false;
    }
    // Argument errors above go straight to stderr with the usage; everything from here on is logged.
    try {
        init_logging(program);
    } catch (const std::exception& err) {
        std::cerr << err.what() << "\n";
        return false;
    }
    return true;
}

std::string command_line(int argc, char** argv) {
    std::string line;
    for (int i = 0; i < argc; i++) line += (i ? " " : "") + std::string(argv[i]);
    return line;
}

coro::Coro<int> async_main(int argc, char** argv) {
    argparse::ArgumentParser program("adsb", "", argparse::default_arguments::help);
    Commands cmds;
    add_options(program, cmds);
    if (!parse_command_line(program, cmds, argc, argv)) co_return 1;
    LOGF(INFO, "adsb %s", adsb::version_info::summary_line());

    const std::optional<uint16_t> ws_port = program.present<uint16_t>("--ws-port");
    const auto ws_interval = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(program.get<double>("--ws-interval")));
    const std::optional<std::string> history_db_path = program.present("--history-db");
    // parse_command_line() has checked that it resolves.
    std::optional<std::string> capture_dir;
    if (const auto dir = program.present("--capture-dir")) capture_dir = *resolve_capture_dir(*dir);

    if (program.is_subcommand_used(cmds.serve)) {
        // AircraftHistory::open() would create a missing file; serving an empty one is never what was meant.
        if (!std::filesystem::exists(*history_db_path)) {
            LOGF(ERROR, "history database %s does not exist", *history_db_path);
            co_return 1;
        }
        std::optional<AircraftHistory> history = AircraftHistory::open(*history_db_path);
        if (!history) co_return 1;
        api_server_spawn(*ws_port, ws_interval, std::nullopt, "", std::move(history));
        LOGF(INFO, "serving %s on port %u (Ctrl+C to stop)", *history_db_path, *ws_port);
        co_await coro::signal(SIGINT);
        LOGF(INFO, "stopping after SIGINT");
        co_return 0;
    }

    try {
        const FilterOptions filter_options = FilterOptions::from_options(program);
        DemodLoopParams loop_params = DemodLoopParams::from_options(program, command_line(argc, argv));

        std::optional<AircraftHistory> history;
        if (history_db_path) {
            history = AircraftHistory::open(*history_db_path);
            // It has logged why; recording was asked for, so don't run without it.
            if (!history) co_return 1;
            LOGF(INFO, "recording aircraft history to %s", *history_db_path);
        }
        std::optional<ApiSenders> ws;
        if (ws_port) {
            std::optional<AircraftHistory> ws_history;
            if (history) ws_history = history->clone();
            api::Settings settings;
            if (capture_dir) settings.set_capture_dir(*capture_dir);
            auto [senders, receivers] = api_channels(std::move(settings));
            // The source's subcommand, for the UI.
            std::string source;
            for (const char* name : {"file", "vita49", "rtlsdr", "sidekiq", "uhd"}) {
                if (program.is_subcommand_used(name)) source = name;
            }
            api_server_spawn(*ws_port, ws_interval, std::move(receivers), std::move(source), std::move(ws_history));
            ws = std::move(senders);
        }

        // The filter is built for the source's rate, and the source pads its blocks for the filter.
        auto make_filter = [&](double rate_hz) {
            try {
                FilterBank filter = filter_options.make(rate_hz);
                LOGF(INFO, "filter: %d taps x %d phases, %s", filter.taps, filter.num_phases, filter.description);
                return filter;
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("filter bank: ") + e.what());
            }
        };
        double rate_hz;
        double freq_hz;
        FilterBank filter;
        std::optional<coro::CoroStream<IqBuffer>> stream;
        if (program.is_subcommand_used(cmds.vita49)) {
            Vita49StreamParams params = Vita49StreamParams::from_options(program, cmds.vita49);
            // A clone of its own, as `ws` goes to run_demod_loop; shared, as on_status must be copyable.
            if (ws)
                params.receiver.on_status =
                    [status = std::make_shared<coro::WatchSender<std::optional<api::InputStatus>>>(
                         ws->input_status.clone())](const InputStatus& s) { (void)status->send(to_proto(s)); };
            rate_hz = params.receiver.sample_rate_hz;
            freq_hz = params.freq_hz;
            filter = make_filter(rate_hz);
            stream.emplace(vita49_iq_stream(std::move(params.receiver), demod_padding(filter, rate_hz),
                                            Vita49StreamParams::kChunkSamples));
        } else if (program.is_subcommand_used(cmds.rtlsdr)) {
            RtlSdrStreamParams params = RtlSdrStreamParams::from_options(program, cmds.rtlsdr);
            rate_hz = params.tuning.rate_hz;
            freq_hz = params.tuning.freq_hz;
            filter = make_filter(rate_hz);
            stream.emplace(rtlsdr_iq_stream(std::move(params), demod_padding(filter, rate_hz)));
#ifdef ADSB_WITH_SIDEKIQ
        } else if (program.is_subcommand_used(cmds.sidekiq)) {
            SidekiqStreamParams params = SidekiqStreamParams::from_options(program, cmds.sidekiq);
            rate_hz = params.rate_hz;
            freq_hz = params.freq_hz;
            filter = make_filter(rate_hz);
            stream.emplace(sidekiq_iq_stream(std::move(params), demod_padding(filter, rate_hz),
                                             SidekiqStreamParams::kChunkSamples));
#endif
#ifdef ADSB_WITH_UHD
        } else if (program.is_subcommand_used(cmds.uhd)) {
            UhdStreamParams params = UhdStreamParams::from_options(program, cmds.uhd);
            rate_hz = params.rate_hz;
            freq_hz = params.freq_hz;
            filter = make_filter(rate_hz);
            stream.emplace(uhd_iq_stream(std::move(params), demod_padding(filter, rate_hz),
                                         UhdStreamParams::kChunkSamples));
#endif
        } else {
            FileStreamParams params = FileStreamParams::from_options(program, cmds.file);
            rate_hz = params.rate_hz;
            freq_hz = params.freq_hz;
            filter = make_filter(rate_hz);
            stream.emplace(file_iq_stream(std::move(params.iq_path), params.format,
                                          demod_padding(filter, rate_hz), FileStreamParams::kChunkSamples));
        }

        // The demod task owns the source stream, and runs until the stream ends or Ctrl+C: this is the one place
        // the signal is handled, as no source watches for it itself. On Ctrl+C, select() drops the demod task's
        // handle, which cancels run_demod_loop and drains it before select() returns. That destroys the stream,
        // and with it the source (its receive task, or its RtlSdr, whose destructor stops the read). A hardware source
        // opens its device when the demod first reads from it, so an error opening it ends the stream as one
        // receiving would, and is logged below.
        // demodulate() splits each block across the runtime's workers; with one it still works, serially.
        if (coro::worker_count() < 2)
            LOGF(WARNING, "the runtime has %u worker(s): the demodulator runs serially",
                 static_cast<unsigned>(coro::worker_count()));
        auto done = co_await coro::select(
            coro::build_task().name("demod").spawn(run_demod_loop(std::move(filter), rate_hz, freq_hz,
                                                                  std::move(*stream), std::move(loop_params),
                                                                  std::move(history), std::move(ws))),
            coro::signal(SIGINT));
        if (done.index() == 1) {
            LOGF(INFO, "stopping after SIGINT");
        } else {
            LOGF(INFO, "stopped: the input ended");
        }
    } catch (const std::exception& e) {
        LOGF(ERROR, "%s", e.what());
        co_return 1;
    }

    co_return 0;
}

}  // namespace

int main(int argc, char *argv[])
{
    // A work-stealing runtime, one worker per hardware thread: the demodulator's kernels run on its workers
    // (demodulate()), alongside the I/O.
    return coro::Runtime().block_on(async_main(argc, argv));
}
