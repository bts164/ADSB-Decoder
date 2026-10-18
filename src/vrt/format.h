#pragma once

// The parts of the VITA-49 (VRT) wire format that both the encoder (encode.h) and the decoder (assembler.h)
// use, as this project profiles it: see doc/design/vita49-format.md. Every packet has a Stream ID, no Class
// ID, no Trailer, and UTC + picosecond timestamps; every word is big-endian, the I/Q payload included.

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vrt {

// Header word, stream id, integer seconds, picoseconds (two words).
constexpr size_t kHeaderBytes = 20;

// Header word fields.
constexpr uint32_t kPacketTypeIfData = 0x1;     // with Stream ID
constexpr uint32_t kPacketTypeIfContext = 0x4;
constexpr uint32_t kTsiUtc = 0b01;
constexpr uint32_t kTsfPicoseconds = 0b10;

// IF Context packet CIF0 bits for the fields this profile sends.
constexpr uint32_t kCifChangeIndicator = 1u << 31;
constexpr uint32_t kCifBandwidth = 1u << 29;
constexpr uint32_t kCifRfReferenceFrequency = 1u << 27;
constexpr uint32_t kCifSampleRate = 1u << 21;

constexpr uint64_t kPsPerSec = 1'000'000'000'000;

// A packet's timestamp: UTC seconds and the picoseconds into that second (< kPsPerSec).
struct Timestamp {
    uint32_t sec;
    uint64_t ps;
};

inline uint32_t load_be32(const uint8_t* p) {
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | uint32_t{p[3]};
}

inline void store_be32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

// Context field values in Hz are 64-bit signed Q44.20 fixed point: Hz * 2^20.
inline int64_t to_q44_20(double hz) { return std::llround(hz * (1 << 20)); }
inline double from_q44_20(int64_t raw) { return static_cast<double>(raw) / (1 << 20); }

}  // namespace vrt
