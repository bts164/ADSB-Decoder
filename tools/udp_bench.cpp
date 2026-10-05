// Loopback UDP microbenchmark for coro::UdpSocket on a narrow, controllable workload (vita49_send is
// the whole-app version).
//
// One process: a paced sender task -> 127.0.0.1 -> a receiver task, each with its own socket.
// Every datagram carries a sequence number in its first 8 bytes, so the receiver reports what
// arrived, what was lost and what was reordered. CPU time (user + system, whole process, so the
// loopback receive softirq that the sender's syscall pays for is included) is divided by datagrams
// delivered -- the number to compare between changes.
//
//   udp_bench --rate 300000 --seconds 5
//   udp_bench --rate 300000 --gso --group 20 --gro
//
// Sender shapes (per pacing tick, --group datagrams):
//   default        --group separate sends of --size bytes each
//   --gso          UDP_SEGMENT set; the tick goes out as ONE buffer of group*size bytes that the
//                  kernel splits (what vita49_send's sim source does)
// Receiver: --gro sets UDP_GRO on the receiving socket, read with recv_segments_from().
//
// Kernel socket buffers are the system defaults (net.core.rmem_default / wmem_default); raise them
// with sysctl if the receiver overflows in bursts.

#include <argparse/argparse.hpp>

#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include <coro/coro.h>
#include <coro/io/socket_address.h>
#include <coro/io/udp_socket.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/sleep.h>
#include <coro/task/join_handle.h>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kSentinelMagic = 0x53544f5053544f50ull;
constexpr std::size_t kSentinelBytes = 16;   // distinct from any --size (which must be >= 64)
constexpr std::size_t kMaxDatagram = 65535;

struct Params {
    std::size_t size = 1468;
    double seconds = 5.0;
    double rate = 200000;   // datagrams per second
    std::size_t group = 1;  // datagrams per pacing tick
    bool gso = false;
    bool gro = false;
    std::uint16_t port = 47990;
};

struct SendSummary {
    std::uint64_t attempted = 0;   // datagrams handed to send()
    double elapsed_s = 0;
};

struct RecvSummary {
    std::uint64_t datagrams = 0;
    std::uint64_t bytes = 0;
    std::uint64_t out_of_order = 0;
};

class RecvCounter {
public:
    // Returns true when the sentinel arrives.
    bool note(const std::byte* p, std::size_t len) {
        std::uint64_t v = 0;
        if (len == kSentinelBytes) {
            std::memcpy(&v, p, 8);
            if (v == kSentinelMagic) return true;
        }
        if (len < 8) return false;
        std::memcpy(&v, p, 8);
        m_sum.datagrams++;
        m_sum.bytes += len;
        if (m_any && v < m_highest) m_sum.out_of_order++;
        m_highest = std::max(m_highest, v);
        m_any = true;
        return false;
    }
    const RecvSummary& summary() const { return m_sum; }

private:
    RecvSummary m_sum;
    std::uint64_t m_highest = 0;
    bool m_any = false;
};

double cpu_seconds() {
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    auto tv = [](const timeval& t) { return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_usec) * 1e-6; };
    return tv(ru.ru_utime) + tv(ru.ru_stime);
}

// ---------------------------------------------------------------------------
// Receivers
// ---------------------------------------------------------------------------

coro::Coro<RecvSummary> receive(coro::UdpSocket sock, bool gro) {
    RecvCounter counter;
    std::vector<std::byte> buf(kMaxDatagram);
    for (;;) {
        if (gro) {
            auto seg = co_await sock.recv_segments_from(std::move(buf));
            buf = std::move(seg.buf);
            const std::size_t step = seg.segment_size ? seg.segment_size : seg.size;
            bool done = false;
            for (std::size_t off = 0; off < seg.size && !done; off += step)
                done = counter.note(buf.data() + off, std::min(step, seg.size - off));
            if (done) break;
        } else {
            auto [n, b] = co_await sock.recv(std::move(buf));
            buf = std::move(b);
            if (counter.note(buf.data(), n)) break;
        }
    }
    co_return counter.summary();
}

// ---------------------------------------------------------------------------
// Senders
// ---------------------------------------------------------------------------

// Fills datagram i of the tick with its sequence number; the rest of the payload is left as is.
void stamp(std::vector<std::byte>& buf, std::size_t index, std::size_t size, std::uint64_t seq) {
    std::memcpy(buf.data() + index * size, &seq, sizeof(seq));
}

std::chrono::nanoseconds tick_period(const Params& p) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(static_cast<double>(p.group) / p.rate));
}

std::vector<std::byte> make_sentinel() {
    std::vector<std::byte> s(kSentinelBytes, std::byte{0});
    std::memcpy(s.data(), &kSentinelMagic, 8);
    return s;
}

coro::Coro<SendSummary> send_paced(coro::UdpSocket sock, Params p) {
    SendSummary sum;
    if (p.gso) sock.set_segment_size(p.size);
    std::vector<std::byte> buf(p.group * p.size, std::byte{0x5a});
    std::uint64_t seq = 0;
    const auto t0 = Clock::now();
    const auto period = tick_period(p);
    const auto end = t0 + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(p.seconds));
    for (std::uint64_t tick = 0; Clock::now() < end; ++tick) {
        // A coro timer (nanosecond resolution on the IoDriver). Ready at once when behind, so a rate
        // above what the socket can take just runs flat out.
        co_await coro::sleep_until(t0 + period * static_cast<std::int64_t>(tick));
        for (std::size_t i = 0; i < p.group; ++i) stamp(buf, i, p.size, seq++);
        if (p.gso) {
            buf = co_await sock.send(std::move(buf));
        } else {
            // group separate sends of size bytes each
            std::vector<std::byte> one(p.size);
            for (std::size_t i = 0; i < p.group; ++i) {
                std::memcpy(one.data(), buf.data() + i * p.size, p.size);
                one = co_await sock.send(std::move(one));
            }
        }
        sum.attempted += p.group;
    }
    sum.elapsed_s = std::chrono::duration<double>(Clock::now() - t0).count();
    if (p.gso) sock.set_segment_size(0);
    // Repeated in case one is lost. The receiver closes its socket on the first one to arrive, so the
    // later sends see ICMP port-unreachable, which a connected socket reports as ECONNREFUSED: that
    // means the receiver is done, so stop.
    for (int i = 0; i < 40; ++i) {
        bool receiver_gone = false;
        try {
            co_await sock.send(make_sentinel());
        } catch (const std::system_error&) {
            receiver_gone = true;
        }
        if (receiver_gone) break;
        co_await coro::sleep_for(std::chrono::milliseconds(5));
    }
    co_return sum;
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

// Wall time, process CPU seconds
struct Mark {
    Clock::time_point wall = Clock::now();
    double cpu = cpu_seconds();
};

coro::Coro<int> async_main(Params p) {
    const auto dest = coro::SocketAddress::parse("127.0.0.1", p.port);
    if (!dest) {
        std::cerr << "bad address\n";
        co_return 2;
    }
    std::printf("size=%zu rate=%.0f/s group=%zu gso=%d gro=%d seconds=%.1f\n", p.size, p.rate, p.group, p.gso,
                p.gro, p.seconds);

    auto rsock = co_await coro::UdpSocket::bind("127.0.0.1", p.port);
    if (p.gro) rsock.set_gro(true);
    auto ssock = co_await coro::UdpSocket::bind("127.0.0.1", 0);
    co_await ssock.connect(*dest);
    coro::JoinHandle<RecvSummary> rx = coro::spawn(receive(std::move(rsock), p.gro));
    const Mark start;
    coro::JoinHandle<SendSummary> tx = coro::spawn(send_paced(std::move(ssock), p));
    const SendSummary s = co_await tx;
    const RecvSummary r = co_await rx;
    const Mark end;

    const double wall = std::chrono::duration<double>(end.wall - start.wall).count();
    const double cpu = end.cpu - start.cpu;
    const std::uint64_t lost = s.attempted > r.datagrams ? s.attempted - r.datagrams : 0;
    std::printf("attempted=%llu delivered=%llu lost=%llu out_of_order=%llu\n",
                static_cast<unsigned long long>(s.attempted), static_cast<unsigned long long>(r.datagrams),
                static_cast<unsigned long long>(lost),
                static_cast<unsigned long long>(r.out_of_order));
    std::printf("send_phase=%.2fs offered=%.0f/s delivered=%.0f/s (%.1f Mbit/s) wall=%.2fs\n", s.elapsed_s,
                s.elapsed_s > 0 ? static_cast<double>(s.attempted) / s.elapsed_s : 0.0,
                s.elapsed_s > 0 ? static_cast<double>(r.datagrams) / s.elapsed_s : 0.0,
                s.elapsed_s > 0 ? static_cast<double>(r.bytes) * 8 / s.elapsed_s / 1e6 : 0.0, wall);
    std::printf("cpu=%.2fs (%.1f%% of one core) cpu_per_delivered=%.2f us\n", cpu, wall > 0 ? cpu / wall * 100 : 0.0,
                r.datagrams ? cpu / static_cast<double>(r.datagrams) * 1e6 : 0.0);
    co_return 0;
}

}  // namespace

int main(int argc, char** argv) {
    argparse::ArgumentParser prog("udp_bench");
    prog.add_argument("--size").default_value(std::size_t{1468}).scan<'u', std::size_t>().help("datagram bytes (>= 64)");
    prog.add_argument("--rate").default_value(200000.0).scan<'g', double>().help("datagrams/s; 0 = flat out");
    prog.add_argument("--seconds").default_value(5.0).scan<'g', double>();
    prog.add_argument("--group").default_value(std::size_t{1}).scan<'u', std::size_t>().help("datagrams per pacing tick");
    prog.add_argument("--gso").default_value(false).implicit_value(true);
    prog.add_argument("--gro").default_value(false).implicit_value(true);
    prog.add_argument("--port").default_value(std::size_t{47990}).scan<'u', std::size_t>();
    try {
        prog.parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n' << prog;
        return 2;
    }
    Params p;
    p.size = prog.get<std::size_t>("--size");
    p.rate = prog.get<double>("--rate");
    p.seconds = prog.get<double>("--seconds");
    p.group = std::max<std::size_t>(1, prog.get<std::size_t>("--group"));
    p.gso = prog.get<bool>("--gso");
    p.gro = prog.get<bool>("--gro");
    p.port = static_cast<std::uint16_t>(prog.get<std::size_t>("--port"));
    if (p.size < 64 || p.group * p.size > 65000) {
        std::cerr << "need size >= 64 and group*size <= 65000\n";
        return 2;
    }
    if (p.rate <= 0) p.rate = 1e9;   // flat out: the pacing sleep is always already due
    return coro::Runtime().block_on(async_main(p));
}
