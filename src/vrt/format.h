#pragma once

// The parts of the VITA-49 (VRT) wire format that both the encoder (encode.h) and the decoder (assembler.h)
// use, as this project profiles it: see doc/design/vita49-format.md. Every packet has a Stream ID, no Class
// ID, no Trailer, and UTC + picosecond timestamps; every word is big-endian, the I/Q payload included.

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

#include "dsp/iq_format.h"

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
constexpr uint32_t kCifDataPayloadFormat = 1u << 15;

// The data payloads this profile sends are iq::OutputFormats (dsp/iq_format.h): complex Cartesian I/Q, each
// component a 16-bit signed integer or a 32-bit IEEE float, big-endian.

// 32-bit words per I/Q pair in format F.
template <class F>
constexpr size_t payload_words_per_sample() {
    static_assert(iq::bytes_per_sample<F>() % 4 == 0, "a sample fills whole 32-bit words");
    return iq::bytes_per_sample<F>() / 4;
}
constexpr size_t payload_words_per_sample(const iq::OutputFormat& format) {
    return std::visit([]<class F>(F) { return payload_words_per_sample<F>(); }, format);
}

// The Data Payload Format field's first word for format F; its second word is 0 (no repeats, a vector size of 1).
// Bits 30-29 are 01 for complex Cartesian, bits 28-24 the item format (0 signed fixed point, 0x0E IEEE single),
// bits 11-6 the packing field size - 1 and bits 5-0 the item size - 1. Bit 31 is 0, processing-efficient
// packing, which is the same for items that fill their 32-bit words.
template <class F>
constexpr uint32_t payload_format_word() {
    constexpr uint32_t bits = 8 * sizeof(typename F::Sample);
    static_assert(F::byte_order == std::endian::big, "every word is big-endian");
    static_assert(F::integer ? std::is_signed_v<typename F::Sample> : bits == 32,
                  "the item formats are signed fixed point and IEEE single");
    return (F::complex ? 1u << 29 : 0) | (F::integer ? 0 : 0x0Eu << 24) | ((bits - 1) << 6) | (bits - 1);
}
constexpr uint32_t payload_format_word(const iq::OutputFormat& format) {
    return std::visit([]<class F>(F) { return payload_format_word<F>(); }, format);
}
// The bits of that word payload_format_word() sets: the rest (packing, tags, the fixed-point radix point) don't
// change how the items are read.
constexpr uint32_t kPayloadFormatItemMask = (3u << 29) | (0x1Fu << 24) | 0xFFFu;

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
