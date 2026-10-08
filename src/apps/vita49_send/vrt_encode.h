#pragma once

// VRT (VITA-49) packet encoding shared by vita49_send's sources: IF Data packets, the periodic IF Context
// packet, and the timestamp model both use. See doc/design/vita49-format.md for the wire format.

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <xtensor/xtensor.hpp>

#include <coro/io/byte_buffer.h>

namespace vita49_send {

// One complete, wire-ready VRT packet (header + big-endian payload), sent as-is.
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
    std::optional<VrtPacket> next_if_due(PacketClock& clock);

private:
    uint32_t m_stream_id;
    StreamConfig m_cfg;
    uint64_t m_interval_samples;
    uint64_t m_next_at = 0;
    uint32_t m_count = 0;  // 4-bit counter, separate from the data packets'
    bool m_first = true;
};

constexpr size_t kVrtHeaderBytes = 20;

// Size on the wire of a VRT IF Data packet of `count` I/Q sample pairs.
constexpr size_t vrt_packet_bytes(size_t count) { return kVrtHeaderBytes + 4 * count; }

// The IF Data packets below: Packet Type 0x1 (with Stream ID), no Class ID, no
// Trailer, UTC + picosecond timestamps, one 32-bit word per I/Q pair, big-endian
// throughout including the payload.

// Writes a whole VRT IF Data packet to `out` (vrt_packet_bytes(count) bytes) from
// `count` interleaved rtlsdr 8-bit offset-binary I/Q pairs (128 == zero). Each
// becomes int16 (b - 128) * 256 on the wire -- the payload's declared 16-bit
// width, not added precision.
void write_vrt_packet_u8(uint8_t* out, uint32_t stream_id, uint32_t packet_count_mod16, PacketClock::Stamp ts,
                         const uint8_t* src, size_t count);

// Writes a whole VRT IF Data packet to `out` (vrt_packet_bytes(count) bytes). `src` is
// `count` interleaved host-order int16 I/Q pairs (the simulator's native output);
// written big-endian as-is, no rescaling.
void write_vrt_packet_i16(uint8_t* out, uint32_t stream_id, uint32_t packet_count_mod16, PacketClock::Stamp ts,
                          const int16_t* src, size_t count);

}  // namespace vita49_send
