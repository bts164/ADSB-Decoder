#include "apps/vita49_send/vrt_encode.h"

namespace vita49_send {
namespace {

inline void store_be32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

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

// Writes the 5-word header of a VRT IF Data packet (Packet Type 0x1: with Stream
// ID), C=0 (no Class ID), T=0 (no Trailer), TSI=01 (UTC), TSF=10 (picoseconds)
// -- see doc/design/vita49-format.md's header word and IF Data packet sections --
// of `count` I/Q sample pairs (one 32-bit word each). Wire format is big-endian
// throughout, including the I/Q payload.
void write_vrt_header(uint8_t* out, uint32_t stream_id, uint32_t packet_count_mod16, PacketClock::Stamp ts,
                      size_t count) {
    const uint32_t packet_size_words = 5 + static_cast<uint32_t>(count);  // header+streamid+tsint+tsfrac(2)+payload
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
}

}  // namespace

std::optional<VrtPacket> ContextSender::next_if_due(PacketClock& clock) {
    if (m_interval_samples == 0 || clock.samples_emitted < m_next_at) return std::nullopt;
    m_next_at = clock.samples_emitted + m_interval_samples;
    VrtPacket pkt = make_context_packet(m_stream_id, m_count, clock.next(0), m_cfg, m_first);
    m_first = false;
    m_count = (m_count + 1) & 0xF;
    return pkt;
}

// The wire sample is int16 (b - 128) * 256 -- recentered to signed and shifted up
// to use the full 16-bit range; this doesn't add precision, it just avoids
// wasting the payload's declared 16-bit width on an 8-bit-scale value. Written
// big-endian, that value's high byte is (int8)(b - 128) == b ^ 0x80 and its low
// byte is always 0, so the convert and the byte swap fuse into one
// XOR-and-zero-interleave pass (auto-vectorizable) written straight into the
// packet -- no intermediate int16 array, no reload for a later swap.
void write_vrt_packet_u8(uint8_t* out, uint32_t stream_id, uint32_t packet_count_mod16, PacketClock::Stamp ts,
                         const uint8_t* src, size_t count) {
    write_vrt_header(out, stream_id, packet_count_mod16, ts, count);
    uint8_t* payload = out + kVrtHeaderBytes;
    for (size_t i = 0; i < count; i++) {
        payload[4 * i] = src[2 * i] ^ 0x80;          // I high byte
        payload[4 * i + 1] = 0;                      // I low byte
        payload[4 * i + 2] = src[2 * i + 1] ^ 0x80;  // Q high byte
        payload[4 * i + 3] = 0;                      // Q low byte
    }
}

void write_vrt_packet_i16(uint8_t* out, uint32_t stream_id, uint32_t packet_count_mod16, PacketClock::Stamp ts,
                          const int16_t* src, size_t count) {
    write_vrt_header(out, stream_id, packet_count_mod16, ts, count);
    uint8_t* payload = out + kVrtHeaderBytes;
    for (size_t i = 0; i < 2 * count; i++) {
        const auto v = static_cast<uint16_t>(src[i]);
        payload[2 * i] = static_cast<uint8_t>(v >> 8);
        payload[2 * i + 1] = static_cast<uint8_t>(v);
    }
}

}  // namespace vita49_send
