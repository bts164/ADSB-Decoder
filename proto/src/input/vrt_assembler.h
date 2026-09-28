#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

/**
 * @file
 * Reorder and gap-fill logic for the VITA-49 (VRT) IQ stream vita49_send emits
 * (doc/design/vita49-format.md). Pure logic with no I/O, so it is unit-testable; vita49_stream.h wraps it
 * in a UDP source.
 */

namespace vrt {

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
    Kind push(const uint8_t* data, size_t len);

    /** Whether the sample rate is known. Data packets are only kept once it is. */
    bool has_rate() const { return m_rate_hz.has_value(); }
    /** The sample rate; requires has_rate(). */
    double sample_rate_hz() const { return *m_rate_hz; }
    std::optional<double> rf_freq_hz() const { return m_rf_freq_hz; }
    std::optional<uint32_t> stream_id() const { return m_stream_id; }

    /**
     * Appends the samples of every packet that has left the reorder window (all of them if `flush`) to
     * `out`, scaled to [-1, 1), in timestamp order, with gaps zero-filled.
     */
    void drain(std::vector<std::complex<float>>& out, bool flush);

    const Stats& stats() const { return m_stats; }

private:
    struct Timestamp {
        uint64_t sec;
        uint64_t ps;
    };
    struct Pending {
        Timestamp ts;
        uint64_t seq;
        std::vector<int16_t> iq;  // interleaved I, Q in host byte order
    };
    void write(Pending&& p, std::vector<std::complex<float>>& out);

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
};

}  // namespace vrt
