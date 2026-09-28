#include "input/vita49_stream.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <coro/io/signal.h>
#include <coro/io/udp_socket.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/select.h>
#include <coro/sync/sleep.h>

#include "input/vrt_assembler.h"

namespace {

constexpr size_t kMaxDatagramBytes = 65536;
constexpr auto kPollInterval = std::chrono::milliseconds(250);  // how often to look at Ctrl+C / idle state
constexpr size_t kChannelBlocks = 8;

coro::Coro<void> wait_for_sigint(std::shared_ptr<std::atomic<bool>> stop) {
    co_await coro::signal(SIGINT);
    std::cerr << "stopping after SIGINT\n";
    stop->store(true);
}

void report(const vrt::Stats& s) {
    std::cerr << "vita49: data packets " << s.data_packets << ", context " << s.context_packets
              << ", zero-filled samples " << s.zero_filled_samples << ", late/duplicate " << s.late_packets
              << ", restarts " << s.restarts << ", other-stream " << s.other_stream_packets << ", invalid "
              << s.invalid_packets << "\n";
}

// Binds the socket and reads datagrams until Ctrl+C or the idle timeout,
// cutting the assembled sample stream into guard-padded IqBlocks. Owns `tx`,
// so the channel closes (ending the consumer's stream) when this returns or
// throws.
coro::Coro<void> receive_loop(Vita49Options opts, coro::MpscSender<IqBlock> tx, IqPadding pad,
                              uint32_t chunk_samples) {
    auto sock = co_await coro::UdpSocket::bind(opts.bind_host, opts.port);
    vrt::Assembler assembler({.sample_rate_hz = opts.sample_rate_hz, .stream_id = opts.stream_id});
    const double idle_timeout_s = opts.idle_timeout_s;
    std::cerr << "listening for VRT/UDP on " << opts.bind_host << ":" << opts.port << " at "
              << opts.sample_rate_hz << " Hz (Ctrl+C to stop)\n";

    auto stop = std::make_shared<std::atomic<bool>>(false);
    auto sigint_watcher = coro::spawn(wait_for_sigint(stop));

    std::vector<std::complex<float>> acc;
    uint64_t next_start = 0;  // counts dropped blocks too, so each block's start stays its true stream index
    uint64_t dropped_blocks = 0;
    auto emit = [&](size_t count, const std::complex<float>* src) {
        IqBlock block = IqBlock::zeroed(pad, count, next_start);
        next_start += count;
        std::copy(src, src + count, block.data());
        // Never block here: stalling would leave the socket unread and the kernel would drop packets anyway.
        if (!tx.try_send(std::move(block)) && ++dropped_blocks % 100 == 1) {
            std::cerr << "[vita49] warning: compute thread falling behind, " << dropped_blocks
                      << " block(s) dropped so far\n";
        }
    };
    auto drain = [&](bool flush) {
        assembler.drain(acc, flush);
        size_t pos = 0;
        for (; acc.size() - pos >= chunk_samples; pos += chunk_samples) emit(chunk_samples, acc.data() + pos);
        if (flush && pos < acc.size()) {
            emit(acc.size() - pos, acc.data() + pos);
            pos = acc.size();
        }
        acc.erase(acc.begin(), acc.begin() + static_cast<std::ptrdiff_t>(pos));
    };

    // The recv future and the poll tick outlive individual select() rounds (via coro::ref) instead of
    // being wrapped in coro::timeout(): cancelling a UdpSocket recv that has already armed libuv
    // leaves it armed with a dangling buffer, so a pending recv must never be cancelled mid-flight.
    auto recv = sock.recv_from(std::vector<std::byte>(kMaxDatagramBytes));
    auto tick = coro::sleep_for(kPollInterval);
    auto last_rx = std::chrono::steady_clock::now();
    bool received = false;
    while (!stop->load()) {
        auto r = co_await coro::select(coro::ref(recv), coro::ref(tick));
        if (r.index() == 1) {
            tick = coro::sleep_for(kPollInterval);
            if (received && idle_timeout_s > 0 &&
                std::chrono::duration<double>(std::chrono::steady_clock::now() - last_rx).count() > idle_timeout_s) {
                std::cerr << "no packets for " << idle_timeout_s << " s; stopping\n";
                break;
            }
            continue;
        }
        auto& [n, buf, from] = std::get<0>(r).value;
        auto kind = assembler.push(reinterpret_cast<const uint8_t*>(buf.data()), n);
        recv = sock.recv_from(std::move(buf));
        if (kind == vrt::Assembler::Kind::Data) {
            received = true;
            last_rx = std::chrono::steady_clock::now();
            drain(false);
        }
    }
    drain(true);
    report(assembler.stats());
}

}  // namespace

coro::CoroStream<IqBlock> vita49_iq_stream(Vita49Options opts, IqPadding pad, uint32_t chunk_samples) {
    auto channel = coro::mpsc_channel<IqBlock>(kChannelBlocks);
    coro::MpscSender<IqBlock> tx = std::move(channel.first);
    coro::MpscReceiver<IqBlock> rx = std::move(channel.second);

    auto receiver = coro::spawn(receive_loop(std::move(opts), std::move(tx), pad, chunk_samples));

    while (auto block = co_await coro::next(rx)) {
        co_yield std::move(*block);
    }
    co_await receiver;  // rethrows a receive_loop failure (a failed bind, or a sample-rate mismatch)
}
