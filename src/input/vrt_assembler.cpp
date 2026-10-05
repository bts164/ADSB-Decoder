#include "input/vrt_assembler.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace vrt {

namespace {

constexpr size_t kHeaderBytes = 20;  // header, stream id, seconds, picoseconds (hi, lo)
constexpr uint32_t kTypeData = 1;
constexpr uint32_t kTypeContext = 4;

uint32_t be32(const uint8_t* p) {
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | uint32_t{p[3]};
}

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
    const uint32_t cif0 = be32(&d[kHeaderBytes]);
    ContextFields out;
    size_t offset = kHeaderBytes + 4;
    for (int bit = 30; bit >= 15; bit--) {
        if (!((cif0 >> bit) & 1)) continue;
        const size_t bytes = 4 * static_cast<size_t>(cif0_words(bit));
        if (offset + bytes > d.size()) return std::nullopt;
        if (bit == 29 || bit == 27 || bit == 21) {
            const auto raw = static_cast<int64_t>((uint64_t{be32(&d[offset])} << 32) | be32(&d[offset+4]));
            const double hz = static_cast<double>(raw) / (1 << 20);  // signed Q44.20
            if (bit == 27) out.rf_freq_hz = hz;
            if (bit == 21) out.sample_rate_hz = hz;
        }
        offset += bytes;
    }
    return out;
}

}  // namespace

Assembler::Assembler(Options opts) : m_opts(opts), m_rate_hz(opts.sample_rate_hz), m_stream_id(opts.stream_id) {}

bool Assembler::later(const Pending& a, const Pending& b) {
    if (a.ts.sec != b.ts.sec) return a.ts.sec > b.ts.sec;
    if (a.ts.ps != b.ts.ps) return a.ts.ps > b.ts.ps;
    return a.seq > b.seq;
}

Assembler::Kind Assembler::push(std::span<const uint8_t> d) {
    auto invalid = [&] {
        m_stats.invalid_packets++;
        return Kind::Invalid;
    };
    if (d.size() < kHeaderBytes || d.size() % 4) return invalid();

    const uint32_t hdr = be32(d.data());
    const uint32_t type = hdr >> 28;
    const bool has_class_id = (hdr >> 27) & 1;
    const bool has_trailer = (hdr >> 26) & 1;
    const uint32_t tsi = (hdr >> 22) & 3;  // 1 = UTC
    const uint32_t tsf = (hdr >> 20) & 3;  // 2 = real-time picoseconds
    if (has_class_id || has_trailer || tsi != 1 || tsf != 2 || (hdr & 0xFFFF) * 4 != d.size()) return invalid();
    if (type != kTypeData && type != kTypeContext) return invalid();

    const uint32_t sid = be32(d.data() + 4);
    const Timestamp ts{be32(d.data() + 8), (uint64_t{be32(d.data() + 12)} << 32) | be32(d.data() + 16)};
    if (ts.ps >= static_cast<uint64_t>(kPsPerSec)) return invalid();

    std::optional<ContextFields> ctx;
    if (type == kTypeContext) {
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
    Pending p{ts, m_seq++, {}};
    if (!m_spare.empty()) {
        p.payload = std::move(m_spare.back());
        m_spare.pop_back();
    }
    // Kept as received (big-endian); write() converts it straight into the sink.
    const size_t bytes = d.size() - kHeaderBytes;
    if (p.payload.size() != bytes) p.payload.resize({bytes});
    std::copy_n(d.data() + kHeaderBytes, bytes, p.payload.data());
    m_pending.emplace_back(std::move(p));
    std::push_heap(m_pending.begin(), m_pending.end(), later);
    return Kind::Data;
}

}  // namespace vrt
