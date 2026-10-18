#include "vrt/encode.h"

namespace vrt {
namespace {

// The header word: TSI=UTC, TSF=picoseconds, C=0 (no Class ID), T=0 (no Trailer).
uint32_t header_word(uint32_t packet_type, uint32_t packet_count_mod16, uint32_t packet_size_words) {
    return (packet_type << 28) | (kTsiUtc << 22) | (kTsfPicoseconds << 20) | ((packet_count_mod16 & 0xF) << 16) |
           (packet_size_words & 0xFFFF);
}

// Header word, stream id and timestamp: the kHeaderBytes every packet starts with.
void write_header(uint8_t* out, uint32_t header, uint32_t stream_id, Timestamp ts) {
    store_be32(out, header);
    store_be32(out + 4, stream_id);
    store_be32(out + 8, ts.sec);
    store_be32(out + 12, static_cast<uint32_t>(ts.ps >> 32));
    store_be32(out + 16, static_cast<uint32_t>(ts.ps & 0xFFFFFFFFu));
}

// Allocates a VRT IF Context packet (Packet Type 0x4), same header conventions
// as the data packets. Body is the CIF0 word followed by the fields it flags,
// in descending bit order, each a 64-bit Q44.20 value.
Packet make_context_packet(uint32_t stream_id, uint32_t packet_count_mod16, Timestamp ts, const StreamConfig& cfg,
                           bool changed) {
    constexpr uint32_t kPacketSizeWords = 5 + 1 + 3 * 2;  // header+sid+ts(3), CIF0, three 64-bit fields

    Packet pkt{xt::xtensor<uint8_t, 1>::from_shape({static_cast<size_t>(kPacketSizeWords) * 4})};
    uint8_t* out = pkt.bytes.data();
    write_header(out, header_word(kPacketTypeIfContext, packet_count_mod16, kPacketSizeWords), stream_id, ts);
    store_be32(out + kHeaderBytes,
               (changed ? kCifChangeIndicator : 0) | kCifBandwidth | kCifRfReferenceFrequency | kCifSampleRate);
    uint8_t* field = out + kHeaderBytes + 4;
    for (double hz : {cfg.bandwidth_hz, cfg.rf_freq_hz, cfg.sample_rate_hz}) {
        const auto q = static_cast<uint64_t>(to_q44_20(hz));
        store_be32(field, static_cast<uint32_t>(q >> 32));
        store_be32(field + 4, static_cast<uint32_t>(q & 0xFFFFFFFFu));
        field += 8;
    }
    return pkt;
}

}  // namespace

// See doc/design/vita49-format.md's header word and IF Data packet sections.
void write_data_header(uint8_t* out, uint32_t stream_id, uint32_t packet_count_mod16, Timestamp ts, size_t count) {
    const uint32_t packet_size_words = 5 + static_cast<uint32_t>(count);  // header+streamid+tsint+tsfrac(2)+payload
    write_header(out, header_word(kPacketTypeIfData, packet_count_mod16, packet_size_words), stream_id, ts);
}

std::optional<Packet> ContextSender::next_if_due(PacketClock& clock) {
    if (m_interval_samples == 0 || clock.samples_emitted < m_next_at) return std::nullopt;
    m_next_at = clock.samples_emitted + m_interval_samples;
    Packet pkt = make_context_packet(m_stream_id, m_count, clock.next(0), m_cfg, m_first);
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
void write_data_packet_u8(uint8_t* out, uint32_t stream_id, uint32_t packet_count_mod16, Timestamp ts,
                          const uint8_t* src, size_t count) {
    write_data_header(out, stream_id, packet_count_mod16, ts, count);
    uint8_t* payload = out + kHeaderBytes;
    for (size_t i = 0; i < count; i++) {
        payload[4 * i] = src[2 * i] ^ 0x80;          // I high byte
        payload[4 * i + 1] = 0;                      // I low byte
        payload[4 * i + 2] = src[2 * i + 1] ^ 0x80;  // Q high byte
        payload[4 * i + 3] = 0;                      // Q low byte
    }
}

}  // namespace vrt
