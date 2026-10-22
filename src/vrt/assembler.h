#pragma once

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include <xtensor/xtensor.hpp>

#include "vrt/format.h"

/**
 * @file
 * Reorder and gap-fill logic for the VITA-49 (VRT) IQ stream vita49_send emits
 * (doc/design/vita49-format.md). Pure logic with no I/O, so it is unit-testable; input/vita49_stream.h wraps
 * it in a UDP source.
 */

namespace vrt {

/**
 * What Assembler::drain() appends reordered, gap-filled samples into. Kept abstract (rather than a
 * concrete buffer type) so Assembler stays decoupled from buffer management: production code
 * (input/vita49_stream.cpp) drains into an IqBlockChunker (input/iq_block_chunker.h), which builds guard-padded
 * IqBuffers directly with no intermediate copy, while tests can drain into a trivial growable buffer.
 *
 * append_with(n, write) appends `n` samples by calling write(dst, offset), possibly several times, to
 * have the samples starting at `offset` of them written straight into `dst`, the interleaved I/Q
 * components of dst.size() / 2 samples. The components are int16 exactly as the packet carries them,
 * big-endian (iq::I16Be in dsp/iq_format.h): Assembler copies each payload into the sink's storage
 * unconverted, and whoever reads the samples does the byte swap.
 */
template<typename S>
concept Sink = requires(S& s, size_t n, void (*write)(std::span<int16_t> dst, size_t offset)) {
    s.append_zeros(n);
    s.append_with(n, write);
};

/** Packet counters, for diagnostics. */
struct Stats {
    uint64_t data_packets = 0;
    uint64_t context_packets = 0;
    uint64_t invalid_packets = 0;
    uint64_t other_stream_packets = 0;
    uint64_t discarded_before_rate = 0;  /**< data packets seen before the sample rate was known */
    uint64_t late_packets = 0;           /**< duplicates, or arrived after their reorder window */
    uint64_t restarts = 0;               /**< timestamp jumps > max_gap_s (sender restarted) */
    uint64_t zero_filled_samples = 0;    /**< stand-ins for packets that never arrived */
    uint64_t samples_received = 0;       /**< real (non-gap-filled) samples actually appended */
};

/**
 * Turns a sequence of VRT datagrams (possibly reordered, duplicated or missing) into a continuous,
 * time-aligned sample sequence.
 *
 * Datagrams are re-sorted by timestamp in a window of Options::reorder_window packets (UDP may reorder,
 * e.g. on WSL2 loopback). A gap between a packet's timestamp and the end of the previous packet is filled
 * with zero samples; a jump larger than Options::max_gap_s is treated as a sender restart and not filled.
 */
class Assembler {
public:
    struct Options {
        std::optional<double> sample_rate_hz;  /**< if unset, taken from the first context packet */
        std::optional<uint32_t> stream_id;     /**< if unset, the first stream seen */
        size_t reorder_window = 256;           /**< packets held for re-sorting */
        double max_gap_s = 1.0;                /**< larger timestamp jumps are sender restarts */
    };

    /** How push() classified a datagram. */
    enum class Kind {
        Data,         /**< IF Data packet, queued for drain() */
        Context,      /**< IF Context packet */
        Invalid,      /**< not a parseable VRT packet */
        OtherStream,  /**< a stream id other than the selected one */
        Discarded,    /**< data packet arriving before the sample rate is known */
    };

    explicit Assembler(Options opts);

    /**
     * Classifies and consumes one datagram. Data packets are held until drain().
     * @throws std::runtime_error if a context packet's sample rate disagrees (> 0.1%) with the rate
     *         already established, since the timestamps would then place samples at the wrong positions
     */
    Kind push(std::span<const uint8_t> data);

    /** Whether the sample rate is known. Data packets are only kept once it is. */
    bool has_rate() const { return m_rate_hz.has_value(); }
    /** The sample rate; requires has_rate(). */
    double sample_rate_hz() const { return *m_rate_hz; }
    std::optional<double> rf_freq_hz() const { return m_rf_freq_hz; }
    std::optional<uint32_t> stream_id() const { return m_stream_id; }

    /**
     * Appends the samples of every packet that has left the reorder window (all of them if `flush`) to
     * `out`, as received (big-endian int16), in timestamp order, with gaps zero-filled.
     */
    template<Sink S>
    void drain(S& out, bool flush);

    const Stats& stats() const { return m_stats; }

private:
    // One queued data packet. Its payload stays in m_payloads and only this small record sits in the
    // m_pending heap: every heap operation moves a few of these, and at several hundred thousand packets
    // a second moving the payload's xtensor (its shape and strides, besides the buffer pointer) with each
    // one was a measurable share of the receive thread.
    struct Pending {
        Timestamp ts;
        uint64_t seq;
        uint32_t slot;  // index of the packet's payload in m_payloads
    };
    template<Sink S>
    void write(const Pending& p, S& out);
    // Heap order for m_pending (earliest timestamp on top). Defined here so the heap algorithms inline it.
    static bool later(const Pending& a, const Pending& b) {
        if (a.ts.sec != b.ts.sec) return a.ts.sec > b.ts.sec;
        if (a.ts.ps != b.ts.ps) return a.ts.ps > b.ts.ps;
        return a.seq > b.seq;
    }

    Options m_opts;
    std::optional<double> m_rate_hz;
    std::optional<double> m_rf_freq_hz;
    std::optional<uint32_t> m_stream_id;
    std::vector<Pending> m_pending;  // heap, earliest timestamp on top
    uint64_t m_seq = 0;
    std::optional<Timestamp> m_base_ts;  // timestamp of the sample at index m_base_index
    int64_t m_base_index = 0;
    int64_t m_next_index = 0;  // index of the next sample to emit
    Stats m_stats;
    // Payload of each pending packet, at Pending::slot: interleaved I, Q as received (big-endian int16).
    // A slot's buffer is reused by later packets, so steady state allocates nothing per packet. Grows to
    // the largest number of packets ever pending at once.
    std::vector<xt::xtensor<uint8_t, 1>> m_payloads;
    std::vector<uint32_t> m_free_slots;  // slots of m_payloads no pending packet uses
};

template<Sink S>
void Assembler::drain(S& out, bool flush) {
    while (!m_pending.empty() && (flush || m_pending.size() > m_opts.reorder_window)) {
        std::pop_heap(m_pending.begin(), m_pending.end(), later);
        const Pending p = m_pending.back();
        m_pending.pop_back();
        write(p, out);
        m_free_slots.push_back(p.slot);
    }
}

template<Sink S>
void Assembler::write(const Pending& p, S& out) {
    const auto rate = static_cast<int64_t>(std::llround(*m_rate_hz));
    const auto max_gap = static_cast<__int128>(m_opts.max_gap_s * static_cast<double>(rate));
    const xt::xtensor<uint8_t, 1>& bytes = m_payloads[p.slot];
    const size_t n = bytes.size() / 4;

    if (!m_base_ts) {
        m_base_ts = p.ts;
        m_base_index = m_next_index;
    }
    auto index_of = [&](const Timestamp& ts) {
        constexpr auto kPs = static_cast<__int128>(kPsPerSec);
        const __int128 dsec = static_cast<__int128>(ts.sec) - static_cast<__int128>(m_base_ts->sec);
        const __int128 dps = dsec * kPs + (static_cast<__int128>(ts.ps) - static_cast<__int128>(m_base_ts->ps));
        const __int128 num = dps * rate + kPs / 2;
        const __int128 whole = num >= 0 ? num / kPs : -((-num + kPs - 1) / kPs);  // floor
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
    const auto gap = static_cast<int64_t>(idx - m_next_index);
    if (gap > 0) {
        out.append_zeros(static_cast<size_t>(gap));
        m_stats.zero_filled_samples += static_cast<uint64_t>(gap);
        m_next_index += gap;
    }

    // The payload goes into the sink as it is: one copy, with the conversion left to the resampler.
    const std::span<const uint8_t> payload(bytes.data(), bytes.size());
    out.append_with(n, [payload](std::span<int16_t> dst, size_t offset) {
        if (4 * offset > payload.size() || dst.size_bytes() > payload.size() - 4 * offset)
            throw std::out_of_range("vrt::Assembler: sink asked for samples past the packet's payload");
        std::memcpy(dst.data(), payload.data() + 4 * offset, dst.size_bytes());
    });
    m_next_index += static_cast<int64_t>(n);
    m_stats.samples_received += n;
}

}  // namespace vrt
