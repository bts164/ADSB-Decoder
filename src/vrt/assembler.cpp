#include "vrt/assembler.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "common/trace.h"
namespace vrt {

namespace {

// IF Context CIF0 fields by bit, size in 32-bit words (doc/design/vita49-format.md).
int cif0_words(int bit) {
    switch (bit) {
        case 30: case 24: case 23: case 22: case 19: case 18: case 16: return 1;
        case 29: case 28: case 27: case 26: case 25: case 21: case 20: case 17: case 15: return 2;
        default: return 0;
    }
}

struct ContextFields {
    std::optional<double> sample_rate_hz, rf_freq_hz;
};

// nullopt if the CIF0 words run past the end of the packet.
std::optional<ContextFields> parse_context(std::span<const uint8_t> d) {
    const uint32_t cif0 = load_be32(&d[kHeaderBytes]);
    ContextFields out;
    size_t offset = kHeaderBytes + 4;
    for (int bit = 30; bit >= 15; bit--) {
        if (!((cif0 >> bit) & 1)) continue;
        const size_t bytes = 4 * static_cast<size_t>(cif0_words(bit));
        if (offset + bytes > d.size()) return std::nullopt;
        const uint32_t flag = 1u << bit;
        if (flag == kCifBandwidth || flag == kCifRfReferenceFrequency || flag == kCifSampleRate) {
            const double hz = from_q44_20(
                static_cast<int64_t>((uint64_t{load_be32(&d[offset])} << 32) | load_be32(&d[offset + 4])));
            if (flag == kCifRfReferenceFrequency) out.rf_freq_hz = hz;
            if (flag == kCifSampleRate) out.sample_rate_hz = hz;
        }
        offset += bytes;
    }
    return out;
}

}  // namespace

Assembler::Assembler(Options opts) : m_opts(opts), m_rate_hz(opts.sample_rate_hz), m_stream_id(opts.stream_id) {}

Assembler::Kind Assembler::push(std::span<const uint8_t> d) {
    TRACE_ZONE("Assembler::push");
    auto invalid = [&] {
        m_stats.invalid_packets++;
        return Kind::Invalid;
    };
    if (d.size() < kHeaderBytes || d.size() % 4) return invalid();

    const uint32_t hdr = load_be32(d.data());
    const uint32_t type = hdr >> 28;
    const bool has_class_id = (hdr >> 27) & 1;
    const bool has_trailer = (hdr >> 26) & 1;
    const uint32_t tsi = (hdr >> 22) & 3;
    const uint32_t tsf = (hdr >> 20) & 3;
    if (has_class_id || has_trailer || tsi != kTsiUtc || tsf != kTsfPicoseconds || (hdr & 0xFFFF) * 4 != d.size())
        return invalid();
    if (type != kPacketTypeIfData && type != kPacketTypeIfContext) return invalid();

    const uint32_t sid = load_be32(d.data() + 4);
    const Timestamp ts{load_be32(d.data() + 8), (uint64_t{load_be32(d.data() + 12)} << 32) | load_be32(d.data() + 16)};
    if (ts.ps >= kPsPerSec) return invalid();

    std::optional<ContextFields> ctx;
    if (type == kPacketTypeIfContext) {
        if (d.size() < kHeaderBytes + 4) return invalid();
        ctx = parse_context(d);
        if (!ctx) return invalid();
    }

    if (!m_stream_id) m_stream_id = sid;
    if (sid != *m_stream_id) {
        m_stats.other_stream_packets++;
        return Kind::OtherStream;
    }

    if (ctx) {
        m_stats.context_packets++;
        if (ctx->sample_rate_hz) {
            const double advertised = *ctx->sample_rate_hz;
            if (!m_rate_hz) {
                m_rate_hz = advertised;
            } else if (std::abs(advertised - *m_rate_hz) > 0.001 * *m_rate_hz) {
                throw std::runtime_error("the stream's sample rate (" + std::to_string(advertised) +
                                         " Hz) disagrees with the established rate (" + std::to_string(*m_rate_hz) +
                                         " Hz); the timestamps would place samples at the wrong positions. "
                                         "Restart the receiver with --rate set to the sender's rate");
            }
        }
        if (ctx->rf_freq_hz) m_rf_freq_hz = ctx->rf_freq_hz;
        return Kind::Context;
    }

    if (!m_rate_hz) {
        m_stats.discarded_before_rate++;
        return Kind::Discarded;
    }
    m_stats.data_packets++;
    uint32_t slot;
    if (!m_free_slots.empty()) {
        slot = m_free_slots.back();
        m_free_slots.pop_back();
    } else {
        slot = static_cast<uint32_t>(m_payloads.size());
        m_payloads.emplace_back();
    }
    // Kept as received (big-endian); write() copies it straight into the sink.
    xt::xtensor<uint8_t, 1>& payload = m_payloads[slot];
    const size_t bytes = d.size() - kHeaderBytes;
    if (payload.size() != bytes) payload.resize({bytes});
    std::copy_n(d.data() + kHeaderBytes, bytes, payload.data());
    m_pending.push_back(Pending{ts, m_seq++, slot});
    std::push_heap(m_pending.begin(), m_pending.end(), later);
    return Kind::Data;
}

}  // namespace vrt
