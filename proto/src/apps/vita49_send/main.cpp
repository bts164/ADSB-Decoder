// Standalone CLI: streams IQ (from an RTL-SDR, or simulated ADS-B) out as VRT
// (VITA-49) IF Data packets over UDP -- a test source playing the role a
// real VITA-49-capable SDR front-end would, for exercising the eventual
// vita49_stream.h reader on the adsb decoder side. See
// doc/design/vita49-format.md for the wire format this implements (IF Data
// packets plus periodic IF Context packets carrying bandwidth, RF reference
// frequency and sample rate; no Class ID, no Trailer).
//
// This is deliberately its own executable, not folded into `adsb` -- it has
// nothing to do with demodulation, and its "source" (rtlsdr) and "sink"
// (UDP) are both different from adsb's own IqBlock-consuming pipeline.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <argparse/argparse.hpp>
#include <rtl-sdr.h>
#include <xtensor/xtensor.hpp>

#include "sim/adsb_sim.h"

#include <coro/sync/interval.h>
#include <coro/coro.h>
#include <coro/io/signal.h>
#include <coro/io/byte_buffer.h>
#include <coro/io/socket_address.h>
#include <coro/io/udp_socket.h>
#include <coro/runtime/runtime.h>
#include <coro/sync/mpsc.h>
#include <coro/sync/sleep.h>
#include <coro/task/spawn_blocking.h>

namespace {

// One complete, wire-ready VRT IF Data packet (header + big-endian payload),
// built directly in the rtlsdr callback and sent as-is by vrt_send_loop.
//
// Storage is an xt::xtensor, whose default container leaves elements
// uninitialized on from_shape() (no zero-fill pass over a buffer we are about
// to overwrite in full). xtensor's own begin()/end() are strided xiterators, not
// contiguous iterators, so they don't satisfy coro::ByteBuffer; the wrapper
// exposes raw pointers instead. Moving the xtensor keeps the heap block (and
// so data()) stable.
struct VrtPacket {
    xt::xtensor<uint8_t, 1> bytes;

    uint8_t* begin() { return bytes.data(); }
    uint8_t* end() { return bytes.data() + bytes.size(); }
    const uint8_t* begin() const { return bytes.data(); }
    const uint8_t* end() const { return bytes.data() + bytes.size(); }
};
static_assert(coro::ByteBuffer<VrtPacket>);

inline void store_be32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

// Timestamp model: neither source provides hardware timestamps, so the wall
// clock is sampled ONCE (at the first packet) as an anchor -- a constant but
// unknown offset from the true time of sample 0 -- and every packet's
// timestamp is that anchor plus samples_emitted / sample_rate, so packets are
// spaced by exactly their sample counts with no jitter. Deliberately not a
// per-packet clock read: a USB callback delivers ~256 KB (tens of ms) that is
// split into hundreds of packets in microseconds, so per-packet wall clock
// reads would all be nearly identical. Limitations (not handled): the RTL
// crystal's ppm error makes this clock drift slowly against UTC (~20 us/s at
// 20 ppm), and samples lost inside librtlsdr are invisible here and would
// silently shift the timeline.
struct PacketClock {
    uint32_t sample_rate_hz;
    bool anchored = false;
    uint64_t anchor_sec = 0;
    uint64_t anchor_psec = 0;      // < 1e12
    uint64_t samples_emitted = 0;  // advances for dropped packets too

    struct Stamp {
        uint32_t sec;
        uint64_t psec;
    };

    // Timestamp for the next packet of `count` samples; advances the clock.
    Stamp next(size_t count) {
        if (!anchored) {
            const auto now = std::chrono::system_clock::now();
            const auto secs = std::chrono::time_point_cast<std::chrono::seconds>(now);
            anchor_sec = static_cast<uint64_t>(secs.time_since_epoch().count());
            anchor_psec = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - secs).count()) * 1000ull;
            anchored = true;
        }
        // Integer math. rem < sample_rate, and rem * 1e12 overflows 64 bits above
        // ~18.4 MHz (a timestamp glitch once a second), hence the 128-bit product.
        constexpr uint64_t kPsPerSec = 1'000'000'000'000ull;
        const uint64_t whole = samples_emitted / sample_rate_hz;
        const uint64_t rem = samples_emitted % sample_rate_hz;
        const uint64_t psec =
            anchor_psec + static_cast<uint64_t>(static_cast<unsigned __int128>(rem) * kPsPerSec / sample_rate_hz);
        samples_emitted += count;
        return {static_cast<uint32_t>(anchor_sec + whole + psec / kPsPerSec), psec % kPsPerSec};
    }
};

// What the IF Context packet advertises. The data packets carry no sample rate
// of their own, so this is how a receiver learns it.
struct StreamConfig {
    double sample_rate_hz;
    double rf_freq_hz;
    double bandwidth_hz;
};

// 64-bit signed Q44.20 fixed point, Hz * 2^20 (see doc/design/vita49-format.md).
inline uint64_t to_q44_20(double hz) { return static_cast<uint64_t>(std::llround(hz * 1048576.0)); }

// Allocates a VRT IF Context packet (Packet Type 0x4), same header conventions
// as the data packets: Stream ID, C=0, TSI=UTC, TSF=picoseconds. Body is the
// CIF0 word followed by the fields it flags, in descending bit order, each a
// 64-bit Q44.20 value.
VrtPacket make_context_packet(uint32_t stream_id, uint32_t packet_count_mod16, PacketClock::Stamp ts,
                              const StreamConfig& cfg, bool changed) {
    constexpr uint32_t kCifChangeIndicator = 1u << 31;
    constexpr uint32_t kCifBandwidth = 1u << 29;
    constexpr uint32_t kCifRfReferenceFrequency = 1u << 27;
    constexpr uint32_t kCifSampleRate = 1u << 21;
    constexpr uint32_t kPacketTypeIfContext = 0x4;
    constexpr uint32_t kTsiUtc = 0b01;
    constexpr uint32_t kTsfPicoseconds = 0b10;
    constexpr uint32_t kPacketSizeWords = 5 + 1 + 3 * 2;  // header+sid+ts(3), CIF0, three 64-bit fields

    VrtPacket pkt{xt::xtensor<uint8_t, 1>::from_shape({static_cast<size_t>(kPacketSizeWords) * 4})};
    uint8_t* out = pkt.bytes.data();
    store_be32(out, (kPacketTypeIfContext << 28) | (kTsiUtc << 22) | (kTsfPicoseconds << 20) |
                        ((packet_count_mod16 & 0xF) << 16) | kPacketSizeWords);
    store_be32(out + 4, stream_id);
    store_be32(out + 8, ts.sec);
    store_be32(out + 12, static_cast<uint32_t>(ts.psec >> 32));
    store_be32(out + 16, static_cast<uint32_t>(ts.psec & 0xFFFFFFFFu));
    store_be32(out + 20, (changed ? kCifChangeIndicator : 0) | kCifBandwidth | kCifRfReferenceFrequency | kCifSampleRate);
    uint8_t* field = out + 24;
    for (double hz : {cfg.bandwidth_hz, cfg.rf_freq_hz, cfg.sample_rate_hz}) {
        const uint64_t q = to_q44_20(hz);
        store_be32(field, static_cast<uint32_t>(q >> 32));
        store_be32(field + 4, static_cast<uint32_t>(q & 0xFFFFFFFFu));
        field += 8;
    }
    return pkt;
}

// Decides when a context packet goes out: before the first data packet, then
// every `interval_s` of signal (counted in samples, so it is deterministic and
// independent of pacing). UDP can lose any one packet and a receiver may join
// late, so the context is repeated rather than sent once. The change indicator
// is set only on the first packet -- the config never changes mid-stream.
class ContextSender {
public:
    ContextSender(uint32_t stream_id, StreamConfig cfg, double interval_s)
        : m_stream_id(stream_id),
          m_cfg(cfg),
          m_interval_samples(interval_s > 0 ? static_cast<uint64_t>(std::llround(interval_s * cfg.sample_rate_hz)) : 0) {}

    // A context packet stamped with the next data sample's time, if one is due
    // (does not advance the clock's sample count); nullopt if disabled.
    std::optional<VrtPacket> next_if_due(PacketClock& clock) {
        if (m_interval_samples == 0 || clock.samples_emitted < m_next_at) return std::nullopt;
        m_next_at = clock.samples_emitted + m_interval_samples;
        VrtPacket pkt = make_context_packet(m_stream_id, m_count, clock.next(0), m_cfg, m_first);
        m_first = false;
        m_count = (m_count + 1) & 0xF;
        return pkt;
    }

private:
    uint32_t m_stream_id;
    StreamConfig m_cfg;
    uint64_t m_interval_samples;
    uint64_t m_next_at = 0;
    uint32_t m_count = 0;  // 4-bit counter, separate from the data packets'
    bool m_first = true;
};

struct StreamCallbackCtx {
    coro::MpscSender<VrtPacket> tx;
    size_t samples_per_packet;
    uint32_t stream_id;
    PacketClock clock;
    ContextSender context;
    uint32_t packet_count = 0;  // 4-bit VRT counter; advances per ENCODED packet, dropped or not
};

// Allocates a VRT IF Data packet (Packet Type 0x1: with Stream ID), C=0 (no Class
// ID), T=0 (no Trailer), TSI=01 (UTC), TSF=10 (picoseconds) -- see
// doc/design/vita49-format.md's header word and IF Data packet sections -- of
// `count` I/Q sample pairs (one 32-bit word each), with the 5-word header
// written and the payload left uninitialized. Wire format is big-endian
// throughout, including the I/Q payload.
VrtPacket make_vrt_packet(uint32_t stream_id, uint32_t packet_count_mod16, PacketClock::Stamp ts, size_t count) {
    const uint32_t packet_size_words = 5 + static_cast<uint32_t>(count);  // header+streamid+tsint+tsfrac(2)+payload
    VrtPacket pkt{xt::xtensor<uint8_t, 1>::from_shape({static_cast<size_t>(packet_size_words) * 4})};
    uint8_t* out = pkt.bytes.data();

    constexpr uint32_t kPacketTypeIfDataWithSid = 0x1;
    constexpr uint32_t kTsiUtc = 0b01;
    constexpr uint32_t kTsfPicoseconds = 0b10;
    const uint32_t header = (kPacketTypeIfDataWithSid << 28) | (kTsiUtc << 22) | (kTsfPicoseconds << 20) |
                             ((packet_count_mod16 & 0xF) << 16) | (packet_size_words & 0xFFFF);
    store_be32(out, header);
    store_be32(out + 4, stream_id);
    store_be32(out + 8, ts.sec);
    store_be32(out + 12, static_cast<uint32_t>(ts.psec >> 32));
    store_be32(out + 16, static_cast<uint32_t>(ts.psec & 0xFFFFFFFFu));
    return pkt;
}

// `src` is `count` interleaved rtlsdr 8-bit offset-binary I/Q pairs (128 == zero).
// The wire sample is int16 (b - 128) * 256 -- recentered to signed and shifted up
// to use the full 16-bit range; this doesn't add precision, it just avoids
// wasting the payload's declared 16-bit width on an 8-bit-scale value. Written
// big-endian, that value's high byte is (int8)(b - 128) == b ^ 0x80 and its low
// byte is always 0, so the convert and the byte swap fuse into one
// XOR-and-zero-interleave pass (auto-vectorizable) written straight into the
// packet -- no intermediate int16 array, no reload for a later swap.
VrtPacket build_vrt_packet_u8(uint32_t stream_id, uint32_t packet_count_mod16, PacketClock::Stamp ts, const uint8_t* src,
                              size_t count) {
    VrtPacket pkt = make_vrt_packet(stream_id, packet_count_mod16, ts, count);
    uint8_t* out = pkt.bytes.data();
    uint8_t* payload = out + 20;
    for (size_t i = 0; i < count; i++) {
        payload[4 * i] = src[2 * i] ^ 0x80;          // I high byte
        payload[4 * i + 1] = 0;                      // I low byte
        payload[4 * i + 2] = src[2 * i + 1] ^ 0x80;  // Q high byte
        payload[4 * i + 3] = 0;                      // Q low byte
    }
    return pkt;
}

// `src` is `count` interleaved host-order int16 I/Q pairs (the simulator's native
// output); written big-endian as-is, no rescaling.
VrtPacket build_vrt_packet_i16(uint32_t stream_id, uint32_t packet_count_mod16, PacketClock::Stamp ts, const int16_t* src,
                               size_t count) {
    VrtPacket pkt = make_vrt_packet(stream_id, packet_count_mod16, ts, count);
    uint8_t* payload = pkt.bytes.data() + 20;
    for (size_t i = 0; i < 2 * count; i++) {
        const auto v = static_cast<uint16_t>(src[i]);
        payload[2 * i] = static_cast<uint8_t>(v >> 8);
        payload[2 * i + 1] = static_cast<uint8_t>(v);
    }
    return pkt;
}

void rtlsdr_vrt_cb(unsigned char* buf, uint32_t len, void* ctx_ptr) {
    auto* ctx = static_cast<StreamCallbackCtx*>(ctx_ptr);
    const size_t n = len / 2;  // interleaved uint8 I/Q pairs
    if (n == 0) return;

    for (size_t offset = 0; offset < n; offset += ctx->samples_per_packet) {
        const size_t count = std::min(ctx->samples_per_packet, n - offset);
        // Best effort: it is repeated, so a drop here costs nothing lasting.
        if (auto context = ctx->context.next_if_due(ctx->clock)) ctx->tx.try_send(std::move(*context));
        const PacketClock::Stamp ts = ctx->clock.next(count);

        VrtPacket pkt = build_vrt_packet_u8(ctx->stream_id, ctx->packet_count, ts, buf + 2 * offset, count);
        // Counted even if the packet is dropped below, so a receiver sees the
        // gap in the 4-bit counter.
        ctx->packet_count = (ctx->packet_count + 1) & 0xF;
        if (!ctx->tx.try_send(std::move(pkt))) {
            // Buffer full -- the UDP send side can't keep up. Drop rather
            // than blocking_send(): see rtlsdr_stream.cpp's identical
            // reasoning (blocking here would desync librtlsdr's USB
            // transfer resubmission and lose far more data).
            static uint64_t dropped = 0;
            if (++dropped % 100 == 1) {
                std::cerr << "[vita49_send] warning: UDP send falling behind, " << dropped
                          << " packet(s) dropped so far\n";
            }
        }
    }
}

// RAII wrapper around a librtlsdr device handle, identical in shape to
// rtlsdr_stream.cpp's private RtlSdr -- not shared via a header since it's
// the only two-line difference (callback type) between the two, and this is
// otherwise a fully independent executable. See rtlsdr_stream.cpp for the
// SIGINT-watcher rationale.
class RtlSdr {
public:
    explicit RtlSdr(int device_index) {
        if (rtlsdr_open(&m_dev, static_cast<uint32_t>(device_index)) != 0) {
            m_dev = nullptr;
            throw std::runtime_error("failed to open rtl-sdr device " + std::to_string(device_index));
        }
        m_sigint_watcher = coro::spawn([this]() -> coro::Coro<void> {
            co_await coro::signal(SIGINT);
            std::cerr << "stopping after SIGINT\n";
            close();
        }());
    }

    ~RtlSdr() { close(); }

    RtlSdr(const RtlSdr&) = delete;
    RtlSdr& operator=(const RtlSdr&) = delete;

    void close() {
        if (nullptr != m_dev) {
            rtlsdr_close(m_dev);
            m_dev = nullptr;
        }
    }

    inline rtlsdr_dev_t* dev() const { return m_dev; }

private:
    rtlsdr_dev_t* m_dev = nullptr;
    coro::JoinHandle<void> m_sigint_watcher;
};

coro::Coro<void> vrt_send_loop(coro::MpscReceiver<VrtPacket> rx, coro::UdpSocket sock) {
    while (auto pkt = co_await coro::next(rx)) {
        // send() throws std::system_error (e.g. ECONNREFUSED, surfaced from an
        // earlier ICMP port-unreachable when no listener is bound) and that
        // aborts the loop. Wrap in try/catch here if best-effort streaming
        // with no listener is ever wanted. See the PERFORMANCE NOTE in main().
        co_await sock.send(std::move(*pkt));
    }
}

coro::Coro<void> wait_for_sigint(std::shared_ptr<std::atomic<bool>> stop) {
    co_await coro::signal(SIGINT);
    std::cerr << "stopping after SIGINT\n";
    stop->store(true);
}

struct SimStreamParams {
    adsb_sim::Config sim;
    size_t samples_per_packet;
    uint32_t stream_id;
    double duration_s;  // <= 0: until SIGINT
    double rf_freq_hz;  // advertised in the context packet; the simulated signal is baseband
    double context_interval_s;
};

// Generates simulated ADS-B IQ and sends it as VRT packets paced to real time:
// every tick, sends however many whole packets the wall clock says are due, then
// sleeps. (coro timers have ~1 ms resolution, and a packet is only ~150 us of
// signal at 2.4 Msps, so per-packet sleeps are not an option.) The IQ content
// depends only on the sim config, not on this pacing.
coro::Coro<void> sim_send_loop(coro::UdpSocket sock, adsb_sim::Simulator& sim, SimStreamParams params) {
    PacketClock clock{params.sim.sample_rate_hz};
    const double sample_rate = params.sim.sample_rate_hz;
    ContextSender context(params.stream_id, StreamConfig{sample_rate, params.rf_freq_hz, sample_rate},
                          params.context_interval_s);
    // The simulator generates a block of whole packets at a time (~10 ms of signal, at most 2M samples)
    // and the block is then cut into packets, which amortizes its per-call setup over many packets. The
    // IQ content does not depend on the block size.
    const size_t spp = params.samples_per_packet;
    const size_t block_packets =
        std::clamp<size_t>(static_cast<size_t>(params.sim.sample_rate_hz * 0.01) / spp, 1, (size_t{1} << 21) / spp + 1);
    std::vector<int16_t> block(2 * block_packets * spp);
    size_t block_used = block_packets;  // packets of `block` already sent; == block_packets: needs refilling

    auto stop = std::make_shared<std::atomic<bool>>(false);
    auto sigint_watcher = coro::spawn(wait_for_sigint(stop));

    // Fixed-cadence wake-ups: tick() sleeps only what is left of the period, and not at all when a pass ran
    // long. How many packets each pass sends comes from the wall clock, not from the cadence.
    coro::IntervalTimer tick(std::chrono::milliseconds(2));

    const double rate = params.sim.sample_rate_hz;
    const auto samples_limit = params.duration_s > 0 ? static_cast<uint64_t>(params.duration_s * rate) : UINT64_MAX;
    const auto t0 = std::chrono::steady_clock::now();
    uint32_t packet_count = 0;
    uint64_t sent = 0;

    while (!stop->load() && sent < samples_limit) {
        const double elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const auto due = static_cast<uint64_t>(elapsed_s * rate);
        while (sent + params.samples_per_packet <= due && sent < samples_limit) {
            if (auto ctx_pkt = context.next_if_due(clock)) co_await sock.send(std::move(*ctx_pkt));
            if (block_used == block_packets) {
                sim.generate(block);
                block_used = 0;
            }
            const int16_t* iq = block.data() + 2 * block_used++ * spp;
            VrtPacket pkt = build_vrt_packet_i16(params.stream_id, packet_count, clock.next(params.samples_per_packet),
                                                 iq, params.samples_per_packet);
            packet_count = (packet_count + 1) & 0xF;
            sent += params.samples_per_packet;
            co_await sock.send(std::move(pkt));
        }
        co_await tick.tick();
    }
}

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
    std::cerr << "streaming VRT/UDP to " << dest->to_string() << " (stream id " << stream_id << ")\n";

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
        params.samples_per_packet = samples_per_packet;
        params.stream_id = stream_id;
        params.duration_s = sim_cmd.get<double>("--duration");
        params.rf_freq_hz = freq_hz;
        params.context_interval_s = context_interval_s;
        std::unique_ptr<adsb_sim::Simulator> sim;
        try {
            params.sim.aircraft = adsb_sim::load_scenario(sim_cmd.get<std::string>("--scenario"));
            sim = std::make_unique<adsb_sim::Simulator>(params.sim);
        } catch (const std::invalid_argument& err) {
            std::cerr << err.what() << "\n";
            co_return 1;
        }
        std::cerr << "simulating ADS-B: rate=" << params.sim.sample_rate_hz << " Hz, seed=" << params.sim.seed
                  << ", snr=" << params.sim.snr_db << " dB, " << params.sim.aircraft.size() << " aircraft"
                  << (params.duration_s > 0 ? "" : " (Ctrl+C to stop)") << "\n";
        for (const auto& a : params.sim.aircraft) {
            std::fprintf(stderr, "  icao=%06x callsign=%.8s lat=%.4f lon=%.4f alt=%dft gs=%.0fkt trk=%.0f vr=%dfpm\n",
                         a.icao, a.callsign.c_str(), a.lat_deg, a.lon_deg, a.alt_ft, a.ground_speed_kt, a.track_deg,
                         a.vrate_fpm);
        }
        co_await sim_send_loop(std::move(sock), *sim, params);
        co_return 0;
    }

    const int device_index = rtl_cmd.get<int>("--device");
    const double gain_db = rtl_cmd.get<double>("--gain");
    const unsigned buf_num = rtl_cmd.get<unsigned>("--buf-num");
    const unsigned buf_len = rtl_cmd.get<unsigned>("--buf-len");

    RtlSdr radio(device_index);
    rtlsdr_set_sample_rate(radio.dev(), static_cast<uint32_t>(rate_hz));
    rtlsdr_set_center_freq(radio.dev(), static_cast<uint32_t>(freq_hz));
    if (gain_db < 0.0) {
        rtlsdr_set_tuner_gain_mode(radio.dev(), 0);  // auto
    } else {
        rtlsdr_set_tuner_gain_mode(radio.dev(), 1);  // manual
        rtlsdr_set_tuner_gain(radio.dev(), static_cast<int>(std::lround(gain_db * 10.0)));
    }
    rtlsdr_reset_buffer(radio.dev());

    std::cerr << "streaming from rtl-sdr device " << device_index << ": rate=" << rate_hz << " Hz, freq=" << freq_hz
              << " Hz, gain=" << (gain_db < 0.0 ? std::string("auto") : std::to_string(gain_db) + " dB")
              << " (Ctrl+C to stop)\n";

    auto channel = coro::mpsc_channel<VrtPacket>(1024);
    coro::MpscSender<VrtPacket> tx = std::move(channel.first);
    coro::MpscReceiver<VrtPacket> rx = std::move(channel.second);

    auto read_handle = coro::spawn_blocking(
        [dev = radio.dev(), tx = std::move(tx), samples_per_packet, stream_id, rate_hz, freq_hz, context_interval_s, buf_num, buf_len]() mutable {
            const auto sample_rate_hz = static_cast<uint32_t>(rate_hz);
            StreamCallbackCtx cb_ctx{std::move(tx), samples_per_packet, stream_id, PacketClock{sample_rate_hz},
                                     ContextSender(stream_id, StreamConfig{rate_hz, freq_hz, rate_hz}, context_interval_s)};
            return rtlsdr_read_async(dev, rtlsdr_vrt_cb, &cb_ctx, buf_num, buf_len);
            // cb_ctx -- and the sender it owns -- destructs here, closing the channel.
        });

    co_await vrt_send_loop(std::move(rx), std::move(sock));

    int rc = co_await read_handle;
    std::cerr << "rtl-sdr read thread ended (rc=" << rc << ")\n";
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
    //    channel in async_main exists to absorb ~365-chunk rtlsdr callback bursts,
    //    which is the right tradeoff at small packet sizes; for large packets use
    //    a shallow channel or recycle a buffer pool instead.
    //
    //  * Batching (sendmmsg / UDP GSO / batching channel items) gave only +7-12%
    //    at <=1460 B and ~0 at >=8 KB on loopback. Deferred until a real-NIC
    //    measurement shows we are actually syscall-bound.
    //
    //  * UdpSocket::send()/recv() are hand-written futures: the first poll() tries
    //    a non-blocking syscall on the calling thread (no allocation, no thread
    //    hop) and only falls back to a libuv-thread hop on EAGAIN. Loopback never
    //    hits the send slow path, but a real NIC can once the kernel send buffer
    //    fills -- measure the fast/slow ratio then. On the recv side (the future
    //    VITA-49 reader) the fast path only hits when a datagram is already
    //    queued, so a consumer that keeps up will take the slow path often.
    return coro::Runtime().block_on(async_main(argc, argv));
}
