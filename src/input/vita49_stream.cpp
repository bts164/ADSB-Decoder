#include "input/vita49_stream.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

#include <argparse/argparse.hpp>
#include <coro/coro.h>
#include <coro/io/udp_socket.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/select.h>
#include <coro/sync/sleep.h>
#include <xtensor/xtensor.hpp>

#include "common/log.h"
#include "common/trace.h"
#include "input/iq_block_chunker.h"
#include "vrt/assembler.h"

namespace {

constexpr size_t kMaxDatagramBytes = 65536;
constexpr auto kPollInterval = std::chrono::milliseconds(250);  // how often to look at the idle state
constexpr size_t kChannelBlocks = (64*1024*1024) / (Vita49StreamParams::kChunkSamples * sizeof(int16_t) * 2);  // 64 MiB of blocks, enough for 1 s at 12 MHz
// Recycled block buffers kept for reuse: one for each block the channel holds, plus the few held outside it (the
// chunker's, the stitcher's, the demodulator's). A buffer beyond that is just freed.
constexpr size_t kPoolBuffers = kChannelBlocks + 8;
constexpr auto kStatusInterval = std::chrono::seconds(2);
// How far below real time a window may run before it counts as slow: packets arrive in bursts, so a window
// edge can fall mid-burst.
constexpr double kRealtimeSlack = 0.02;

// Datagrams the kernel has dropped on the UDP socket(s) bound to `port` because the receive buffer was
// full, from /proc/net/udp{,6}'s last column; nullopt where that isn't available.
std::optional<uint64_t> kernel_udp_drops(uint16_t port) {
    uint64_t total = 0;
    bool found = false;
    for (const char* path : {"/proc/net/udp", "/proc/net/udp6"}) {
        std::ifstream file(path);
        std::string line;
        std::getline(file, line);  // column headings
        while (std::getline(file, line)) {
            std::istringstream fields(line);
            std::string slot, local, field, last;
            fields >> slot >> local;  // local is ADDR:PORT, in hex
            const auto colon = local.rfind(':');
            if (colon == std::string::npos || std::stoul(local.substr(colon + 1), nullptr, 16) != port) continue;
            while (fields >> field) last = field;
            total += std::stoull(last);
            found = true;
        }
    }
    return found ? std::optional(total) : std::nullopt;
}

// Turns the running counters into an InputStatus per window of wall-clock time. A window ends at its last
// data packet, not at the moment it's reported, so a source going quiet doesn't read as a slow one.
class InputMonitor {
public:
    InputMonitor(double rate_hz, uint16_t port) : m_rate(rate_hz), m_port(port) {}

    void on_data(std::chrono::steady_clock::time_point now) {
        if (!m_started) {
            m_started = true;
            m_window_start = m_total_start = now;
            m_window_kernel = m_total_kernel = kernel_udp_drops(m_port);
        }
        m_last_rx = now;
    }

    void on_dropped(size_t samples) { m_dropped += samples; }

    // The status of the window that has run its course by `now`, if one has.
    std::optional<InputStatus> poll(std::chrono::steady_clock::time_point now, const vrt::Stats& s) {
        if (!m_started || now - m_window_start < kStatusInterval) return std::nullopt;
        if (m_last_rx <= m_window_start) {  // nothing arrived: the idle timeout deals with that
            m_window_start = now;
            return std::nullopt;
        }
        const auto kernel = kernel_udp_drops(m_port);
        InputStatus status = make(m_window_start, m_window, m_window_kernel, kernel, s);
        m_window_start = m_last_rx;
        m_window = counters(s);
        m_window_kernel = kernel;
        return status;
    }

    // The status over everything from the first data packet to the last.
    std::optional<InputStatus> total(const vrt::Stats& s) const {
        if (!m_started) return std::nullopt;
        return make(m_total_start, {}, m_total_kernel, kernel_udp_drops(m_port), s);
    }

private:
    struct Counters {
        uint64_t stream = 0, lost = 0, dropped = 0;
    };

    Counters counters(const vrt::Stats& s) const {
        return {s.samples_received + s.zero_filled_samples, s.zero_filled_samples, m_dropped};
    }

    InputStatus make(std::chrono::steady_clock::time_point start, Counters from, std::optional<uint64_t> kernel_from,
                     std::optional<uint64_t> kernel_to, const vrt::Stats& s) const {
        const Counters to = counters(s);
        InputStatus status;
        status.window_s = std::chrono::duration<double>(m_last_rx - start).count();
        status.rate_hz = m_rate;
        status.stream_s = static_cast<double>(to.stream - from.stream) / m_rate;
        status.lost_s = static_cast<double>(to.lost - from.lost) / m_rate;
        status.dropped_s = static_cast<double>(to.dropped - from.dropped) / m_rate;
        if (kernel_from && kernel_to) status.kernel_drops = *kernel_to - *kernel_from;
        return status;
    }

    double m_rate;
    uint16_t m_port;
    bool m_started = false;
    std::chrono::steady_clock::time_point m_total_start, m_window_start, m_last_rx;
    Counters m_window;
    std::optional<uint64_t> m_total_kernel, m_window_kernel;
    uint64_t m_dropped = 0;  // samples in blocks dropped because the demodulator was behind
};

void report(const vrt::Stats& s, uint64_t dropped_blocks, const std::optional<InputStatus>& total) {
    LOGF(INFO,
         "data packets %u, context %u, zero-filled samples %u, late/duplicate %u, restarts %u, other-stream %u, "
         "invalid %u, discarded before rate known %u, blocks dropped (compute thread behind) %u",
         s.data_packets, s.context_packets, s.zero_filled_samples, s.late_packets, s.restarts,
         s.other_stream_packets, s.invalid_packets, s.discarded_before_rate, dropped_blocks);
    if (total) {
        LOGF(LEVEL(total->ok() ? absl::LogSeverity::kInfo : absl::LogSeverity::kWarning), "whole run: %s",
             total->describe());
    }
}

}  // namespace

bool InputStatus::ok() const {
    return realtime() >= 1 - kRealtimeSlack && lost_s == 0 && dropped_s == 0 && kernel_drops.value_or(0) == 0;
}

std::string InputStatus::describe() const {
    auto pct = [](double f) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%.1f%%", f * 100);
        return std::string(buf);
    };
    char head[128];
    std::snprintf(head, sizeof head, "%s of the %.1f Msps real-time signal demodulated over %.1f s",
                  pct(delivered()).c_str(), rate_hz / 1e6, window_s);
    std::string out = head;
    if (realtime() < 1 - kRealtimeSlack) {
        out += "; the source sent only " + pct(realtime()) + " of real time";
    }
    if (lost_s > 0 || kernel_drops.value_or(0) > 0) {
        out += "; " + pct(lost_s / window_s) + " lost in transit";
        if (kernel_drops.value_or(0) > 0) {
            out += " (" + std::to_string(*kernel_drops) + " times the kernel dropped data: socket receive buffer full)";
        }
    }
    if (dropped_s > 0) out += "; " + pct(dropped_s / window_s) + " dropped because the demodulator fell behind";
    return out;
}

namespace {

// Binds the socket and reads datagrams until the idle timeout, or until it is cancelled because
// vita49_iq_stream() was dropped (Ctrl+C is handled in main(), not here), cutting the assembled sample stream
// into guard-padded IqBlocks. Owns `tx`, so the channel closes (ending the consumer's stream) when this returns
// or throws.
coro::Coro<void> receive_loop(Vita49Options opts, coro::MpscSender<IqBlock> tx, IqPadding pad,
                              uint32_t chunk_samples) {
    auto sock = co_await coro::UdpSocket::bind(opts.bind_host, opts.port);
    vrt::Assembler assembler({.sample_rate_hz = opts.sample_rate_hz, .stream_id = opts.stream_id});
    const double idle_timeout_s = opts.idle_timeout_s;
    LOGF(INFO, "listening for VRT/UDP on %s:%u at %g Hz (Ctrl+C to stop)", opts.bind_host, opts.port,
         opts.sample_rate_hz);

    // Reordered, gap-filled samples land here (Assembler::drain() appends; see vrt/assembler.h's
    // Sink concept), building chunk_samples-sized IqBlocks directly in place -- a completed block is
    // moved straight out via pop() with no copy from an intermediate accumulator. The blocks hold the
    // packets' big-endian int16 as received; the resampler reads that format directly. Each block's buffer
    // comes back through the pool when the consumer (or send_ready(), for a dropped block) is done with it, so
    // in steady state no block allocates, and none page-faults as its fresh memory is first written.
    auto [free_tx, free_rx] = coro::mpsc_channel<iq::SamplesVariant>(kPoolBuffers);
    IqBlockChunker<iq::I16Be> chunker(pad, chunk_samples,
                                      std::make_shared<coro::MpscSender<iq::SamplesVariant>>(std::move(free_tx)),
                                      std::move(free_rx));
    uint64_t dropped_blocks = 0;
    InputMonitor monitor(opts.sample_rate_hz, opts.port);
    // Reported however the loop ends: on the idle timeout, or when this task is cancelled (the frame is
    // then destroyed where it is suspended, so only a destructor still runs).
    struct Report {
        const vrt::Assembler& assembler;
        const uint64_t& dropped_blocks;
        InputMonitor& monitor;
        ~Report() { report(assembler.stats(), dropped_blocks, monitor.total(assembler.stats())); }
    } final_report{assembler, dropped_blocks, monitor};
    auto send_ready = [&] {
        TRACE_ZONE("vrt_send_ready");
        while (chunker.ready() > 0) {
            IqBlock block = chunker.pop();
            const size_t n = block.n;
            // Never block here: stalling would leave the socket unread and the kernel would drop packets anyway.
            if (!tx.try_send(std::move(block))) {
                TRACE_MESSAGE("vrt block dropped: demodulator behind");
                dropped_blocks++;
                TRACE_PLOT("vrt blocks dropped", static_cast<int64_t>(dropped_blocks));
                monitor.on_dropped(n);
            }
        }
    };
    bool was_ok = true;
    // The kernel's drops (socket receive buffer full) per kPollInterval, plotted to line them up with the
    // zones. Read here rather than on the tick: each select() polls recv first, so while this loop is behind
    // and a datagram is always waiting, the tick never wins.
    std::optional<uint64_t> plotted_kernel_drops;
    std::chrono::steady_clock::time_point next_kernel_plot{};
    auto check_status = [&](std::chrono::steady_clock::time_point now) {
        TRACE_ZONE("vrt_check_status");
        if constexpr (trace::kEnabled) {
            if (now >= next_kernel_plot) {
                next_kernel_plot = now + kPollInterval;
                if (const auto drops = kernel_udp_drops(opts.port)) {
                    if (plotted_kernel_drops) {
                        TRACE_PLOT("vrt kernel drops per 250 ms",
                                   static_cast<int64_t>(*drops - *plotted_kernel_drops));
                    }
                    plotted_kernel_drops = drops;
                }
            }
        }
        const auto status = monitor.poll(now, assembler.stats());
        if (!status) return;
        if (!status->ok()) {
            LOGF(WARNING, "%s", status->describe());
        } else if (!was_ok) {
            LOGF(INFO, "back to full rate: %s", status->describe());
        }
        was_ok = status->ok();
        if (opts.on_status) opts.on_status(*status);
    };
    auto drain = [&](bool flush) {
        {
            TRACE_ZONE("Assembler::drain");
            assembler.drain(chunker, flush);
        }
        if (flush) {
            TRACE_ZONE("IqBlockChunker::flush");
            chunker.flush();
        }
        send_ready();
    };

    // The recv future and the poll tick outlive individual select() rounds (via coro::ref) instead of
    // being wrapped in coro::timeout(). Dropping a pending UdpSocket recv is safe (it's a leaf future
    // that leaves only a weak waker behind and consumes no datagram), but keeping it avoids
    // reallocating the receive buffer every poll interval.
    // GRO: the kernel may queue a run of same-sized datagrams (e.g. a GSO sender's batch) as one buffer,
    // read in one syscall. It roughly doubles how many datagrams the receive buffer holds and cuts the
    // kernel's per-datagram cost about 4x. Without it (not Linux) every read is one datagram.
    try {
        sock.set_gro(true);
    } catch (const std::system_error& e) {
        LOGF(WARNING, "UDP GRO unavailable (%s); reading one datagram at a time", e.what());
    }
    auto recv = sock.recv_segments_from(xt::xtensor<std::byte, 1>::from_shape({kMaxDatagramBytes}));
    auto tick = coro::sleep_for(kPollInterval);
    auto last_rx = std::chrono::steady_clock::now();
    bool received = false;
    for (;;) {
        auto r = co_await coro::select(coro::ref(recv), coro::ref(tick));
        // Zones from here to the end of the iteration only: none may span the co_await above. What a
        // "task poll" zone of this task holds outside them is the select() and the recv syscalls.
        if (r.index() == 1) {
            TRACE_ZONE("vrt_tick");
            tick = coro::sleep_for(kPollInterval);
            check_status(std::chrono::steady_clock::now());
            if (received && idle_timeout_s > 0 &&
                std::chrono::duration<double>(std::chrono::steady_clock::now() - last_rx).count() > idle_timeout_s) {
                LOGF(INFO, "no packets for %g s; stopping", idle_timeout_s);
                break;
            }
            continue;
        }
        TRACE_ZONE("vrt_datagrams");
        auto& [n, buf, from, seg] = std::get<0>(r).value;
        bool data = false;
        {
            TRACE_ZONE("vrt_push_segments");
            for (size_t off = 0; off < n; off += seg) {
                data |= assembler.push(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(buf.data()) + off,
                                                                std::min(seg, n - off))) == vrt::Assembler::Kind::Data;
            }
        }
        recv = sock.recv_segments_from(std::move(buf));
        if (data) {
            last_rx = std::chrono::steady_clock::now();
            received = true;
            {
                TRACE_ZONE("vrt_monitor_on_data");
                monitor.on_data(last_rx);
            }
            drain(false);
            check_status(last_rx);
        }
    }
    drain(true);
}

}  // namespace

coro::CoroStream<IqBlock> vita49_iq_stream(Vita49Options opts, IqPadding pad, uint32_t chunk_samples) {
    auto channel = coro::mpsc_channel<IqBlock>(kChannelBlocks);
    coro::MpscSender<IqBlock> tx = std::move(channel.first);
    coro::MpscReceiver<IqBlock> rx = std::move(channel.second);

    auto task = coro::build_task()
        .name("vita49_receive")
        .spawn(receive_loop(std::move(opts), std::move(tx), pad, chunk_samples));

    while (auto block = co_await coro::next(rx)) {
        co_yield std::move(*block);
    }
    co_await task;  // rethrows a receive_loop failure (a failed bind, or a sample-rate mismatch)
}

void Vita49StreamParams::add_options(argparse::ArgumentParser& cmd) {
    cmd.add_description("Receive a live VITA-49/VRT IQ stream over UDP (as sent by vita49_send).");
    cmd.add_argument("--port").scan<'u', unsigned>().required().help("UDP port to receive VRT packets on");
    cmd.add_argument("--bind").default_value(std::string("0.0.0.0")).help("address to bind");
    cmd.add_argument("--stream-id").scan<'u', unsigned>().help(
        "only accept this VRT stream id (default: the first one seen)");
    cmd.add_argument("--idle-timeout").scan<'g', double>().default_value(2.0).help(
        "stop after this many seconds without packets, once the first has arrived (0 = never)");
    cmd.add_argument("--rate").scan<'g', double>().required().help(
        "the sender's sample rate in Hz; an IF Context packet that gives a different one ends the stream");
    cmd.add_argument("--freq").scan<'g', double>().default_value(1090e6).help(
        "center frequency in Hz, used to label published spectrum frames");
}

Vita49StreamParams Vita49StreamParams::from_options(const argparse::ArgumentParser& /*program*/,
                                                    const argparse::ArgumentParser& cmd) {
    return {
        .receiver =
            {
                .bind_host = cmd.get<std::string>("--bind"),
                .port = static_cast<uint16_t>(cmd.get<unsigned>("--port")),
                .sample_rate_hz = cmd.get<double>("--rate"),
                .stream_id = cmd.present<unsigned>("--stream-id"),
                .idle_timeout_s = cmd.get<double>("--idle-timeout"),
                .on_status = {},
            },
        .freq_hz = cmd.get<double>("--freq"),
    };
}
