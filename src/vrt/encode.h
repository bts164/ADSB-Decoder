#pragma once

// VRT (VITA-49) packet encoding: IF Data packets, the periodic IF Context packet, and the timestamp model both
// use. See format.h for the profile and doc/design/vita49-format.md for the wire format.

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <xtensor/xtensor.hpp>

#include "vrt/format.h"

namespace vrt {

// One complete, wire-ready VRT packet (header + big-endian payload), sent as-is. An xt::xtensor because
// from_shape() leaves the bytes uninitialized: no zero-fill pass over a buffer about to be written in full.
struct Packet {
    xt::xtensor<uint8_t, 1> bytes;
};

// Timestamp model, for a source without hardware timestamps: the wall clock is sampled ONCE (at the first
// packet) as an anchor -- a constant but unknown offset from the true time of sample 0 -- and every packet's
// timestamp is that anchor plus samples_emitted / sample_rate, so packets are spaced by exactly their sample
// counts with no jitter. Deliberately not a per-packet clock read: an RTL-SDR's USB callback, for one,
// delivers ~256 KB (tens of ms) that is split into hundreds of packets in microseconds, so per-packet wall
// clock reads would all be nearly identical. Limitations (not handled): the device crystal's ppm error makes
// this clock drift slowly against UTC (~20 us/s at 20 ppm), and samples the device loses before they reach
// the source are invisible here and would silently shift the timeline.
struct PacketClock {
    uint32_t sample_rate_hz;
    bool anchored = false;
    uint64_t anchor_sec = 0;
    uint64_t anchor_ps = 0;        // < kPsPerSec
    uint64_t samples_emitted = 0;  // advances for dropped packets too

    // Timestamp for the next packet of `count` samples; advances the clock.
    Timestamp next(size_t count) {
        if (!anchored) {
            const auto now = std::chrono::system_clock::now();
            const auto secs = std::chrono::time_point_cast<std::chrono::seconds>(now);
            anchor_sec = static_cast<uint64_t>(secs.time_since_epoch().count());
            anchor_ps = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - secs).count()) * 1000ull;
            anchored = true;
        }
        // Integer math. rem < sample_rate, and rem * 1e12 overflows 64 bits above
        // ~18.4 MHz (a timestamp glitch once a second), hence the 128-bit product.
        const uint64_t whole = samples_emitted / sample_rate_hz;
        const uint64_t rem = samples_emitted % sample_rate_hz;
        const uint64_t ps =
            anchor_ps + static_cast<uint64_t>(static_cast<unsigned __int128>(rem) * kPsPerSec / sample_rate_hz);
        samples_emitted += count;
        return {static_cast<uint32_t>(anchor_sec + whole + ps / kPsPerSec), ps % kPsPerSec};
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
    std::optional<Packet> next_if_due(PacketClock& clock);

private:
    uint32_t m_stream_id;
    StreamConfig m_cfg;
    uint64_t m_interval_samples;
    uint64_t m_next_at = 0;
    uint32_t m_count = 0;  // 4-bit counter, separate from the data packets'
    bool m_first = true;
};

// Size on the wire of a VRT IF Data packet of `count` I/Q sample pairs.
constexpr size_t data_packet_bytes(size_t count) { return kHeaderBytes + 4 * count; }

// The IF Data packets below: Packet Type 0x1 (with Stream ID), one 32-bit word per I/Q pair.

// Writes a whole VRT IF Data packet to `out` (data_packet_bytes(count) bytes) from
// `count` interleaved 8-bit offset-binary I/Q pairs (128 == zero, as an RTL-SDR delivers them). Each
// becomes int16 (b - 128) * 256 on the wire -- the payload's declared 16-bit
// width, not added precision.
void write_data_packet_u8(uint8_t* out, uint32_t stream_id, uint32_t packet_count_mod16, Timestamp ts,
                          const uint8_t* src, size_t count);

// Writes only the header of a VRT IF Data packet of `count` I/Q pairs to `out`
// (kHeaderBytes bytes), for a caller that fills the payload at out + kHeaderBytes
// itself with big-endian int16 I/Q pairs (e.g. a simulator rendering straight into it).
void write_data_header(uint8_t* out, uint32_t stream_id, uint32_t packet_count_mod16, Timestamp ts, size_t count);

}  // namespace vrt
