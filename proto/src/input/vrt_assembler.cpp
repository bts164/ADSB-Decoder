#include "input/vrt_assembler.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace vrt {

namespace {

constexpr size_t kHeaderBytes = 20;  // header, stream id, seconds, picoseconds (hi, lo)
constexpr int64_t kPsPerSec = 1'000'000'000'000;
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
std::optional<ContextFields> parse_context(const uint8_t* d, size_t len) {
    const uint32_t cif0 = be32(d + kHeaderBytes);
    ContextFields out;
    size_t offset = kHeaderBytes + 4;
    for (int bit = 30; bit >= 15; bit--) {
        if (!((cif0 >> bit) & 1)) continue;
        const size_t bytes = 4 * static_cast<size_t>(cif0_words(bit));
        if (offset + bytes > len) return std::nullopt;
        if (bit == 29 || bit == 27 || bit == 21) {
            const auto raw = static_cast<int64_t>((uint64_t{be32(d + offset)} << 32) | be32(d + offset + 4));
            const double hz = static_cast<double>(raw) / (1 << 20);  // signed Q44.20
            if (bit == 27) out.rf_freq_hz = hz;
            if (bit == 21) out.sample_rate_hz = hz;
        }
        offset += bytes;
    }
    return out;
}

// Heap order for Assembler::m_pending (earliest timestamp on top). Generic only because
// Assembler::Pending is a private type this file-local function can't name.
bool later(const auto& a, const auto& b) {
    if (a.ts.sec != b.ts.sec) return a.ts.sec > b.ts.sec;
    if (a.ts.ps != b.ts.ps) return a.ts.ps > b.ts.ps;
    return a.seq > b.seq;
}

}  // namespace

Assembler::Assembler(Options opts) : m_opts(opts), m_rate_hz(opts.sample_rate_hz), m_stream_id(opts.stream_id) {}

Assembler::Kind Assembler::push(const uint8_t* d, size_t len) {
    auto invalid = [&] {
        m_stats.invalid_packets++;
        return Kind::Invalid;
    };
    if (len < kHeaderBytes || len % 4) return invalid();

    const uint32_t hdr = be32(d);
    const uint32_t type = hdr >> 28;
    const bool has_class_id = (hdr >> 27) & 1;
    const bool has_trailer = (hdr >> 26) & 1;
    const uint32_t tsi = (hdr >> 22) & 3;  // 1 = UTC
    const uint32_t tsf = (hdr >> 20) & 3;  // 2 = real-time picoseconds
    if (has_class_id || has_trailer || tsi != 1 || tsf != 2 || (hdr & 0xFFFF) * 4 != len) return invalid();
    if (type != kTypeData && type != kTypeContext) return invalid();

    const uint32_t sid = be32(d + 4);
    const Timestamp ts{be32(d + 8), (uint64_t{be32(d + 12)} << 32) | be32(d + 16)};
    if (ts.ps >= static_cast<uint64_t>(kPsPerSec)) return invalid();

    std::optional<ContextFields> ctx;
    if (type == kTypeContext) {
        if (len < kHeaderBytes + 4) return invalid();
        ctx = parse_context(d, len);
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
    const size_t nvals = (len - kHeaderBytes) / 2;
    p.iq.resize(nvals);
    for (size_t i = 0; i < nvals; i++) {
        const uint8_t* s = d + kHeaderBytes + 2 * i;
        p.iq[i] = static_cast<int16_t>((uint16_t{s[0]} << 8) | s[1]);
    }
    m_pending.push_back(std::move(p));
    std::push_heap(m_pending.begin(), m_pending.end(), later<Pending, Pending>);
    return Kind::Data;
}

void Assembler::drain(std::vector<std::complex<float>>& out, bool flush) {
    while (!m_pending.empty() && (flush || m_pending.size() > m_opts.reorder_window)) {
        std::pop_heap(m_pending.begin(), m_pending.end(), later<Pending, Pending>);
        Pending p = std::move(m_pending.back());
        m_pending.pop_back();
        write(std::move(p), out);
    }
}

void Assembler::write(Pending&& p, std::vector<std::complex<float>>& out) {
    const auto rate = static_cast<int64_t>(std::llround(*m_rate_hz));
    const auto max_gap = static_cast<__int128>(m_opts.max_gap_s * static_cast<double>(rate));
    const size_t n = p.iq.size() / 2;

    if (!m_base_ts) {
        m_base_ts = p.ts;
        m_base_index = m_next_index;
    }
    auto index_of = [&](const Timestamp& ts) {
        const __int128 dsec = static_cast<__int128>(ts.sec) - static_cast<__int128>(m_base_ts->sec);
        const __int128 dps = dsec * kPsPerSec + (static_cast<__int128>(ts.ps) - static_cast<__int128>(m_base_ts->ps));
        const __int128 num = dps * rate + kPsPerSec / 2;
        const __int128 whole = num >= 0 ? num / kPsPerSec : -((-num + kPsPerSec - 1) / kPsPerSec);  // floor
        return static_cast<__int128>(m_base_index) + whole;
    };

    __int128 idx = index_of(p.ts);
    if (idx > m_next_index + max_gap || idx < m_next_index - max_gap) {
        m_stats.restarts++;
        m_base_ts = p.ts;
        m_base_index = m_next_index;
        idx = m_next_index;
    }
    if (idx < m_next_index) {
        m_stats.late_packets++;
        return;
    }
    if (const auto gap = static_cast<int64_t>(idx - m_next_index); gap > 0) {
        out.insert(out.end(), static_cast<size_t>(gap), std::complex<float>{});
        m_stats.zero_filled_samples += static_cast<uint64_t>(gap);
        m_next_index += gap;
    }

    constexpr float kScale = 1.0f / 32768.0f;
    const size_t base = out.size();
    out.resize(base + n);
    for (size_t i = 0; i < n; i++) {
        out[base + i] = {static_cast<float>(p.iq[2 * i]) * kScale, static_cast<float>(p.iq[2 * i + 1]) * kScale};
    }
    m_next_index += static_cast<int64_t>(n);
}

}  // namespace vrt
