// Unit tests for vrt::Assembler (vrt_assembler.h). Packets are serialized by
// hand here, independently of vita49_send.cpp's builders, per
// doc/design/vita49-format.md.

#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <stdexcept>
#include <vector>

#include "input/vrt_assembler.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

using Bytes = std::vector<uint8_t>;

// Minimal Sink (vrt_assembler.h) for tests: appends into a plain vector, with the same
// size()/operator[] surface the tests already assert against.
struct Samples {
    std::vector<std::complex<float>> data;
    void append_zeros(size_t n) { data.insert(data.end(), n, std::complex<float>{}); }
    template<typename Writer>
    void append_with(size_t n, Writer&& write) {
        data.resize(data.size() + n);
        // An array of std::complex<float> is addressable as its interleaved real/imaginary floats.
        write(std::span<float>(reinterpret_cast<float*>(data.data() + data.size() - n), 2 * n), 0);
    }
    size_t size() const { return data.size(); }
    std::complex<float>& operator[](size_t i) { return data[i]; }
    const std::complex<float>& operator[](size_t i) const { return data[i]; }
};

void put32(Bytes& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s));
}

// Header + stream id + UTC seconds + picoseconds; `hdr_type` is the packet type nibble.
Bytes header(uint32_t hdr_type, uint32_t words, uint32_t sid, uint64_t sec, uint64_t ps) {
    Bytes b;
    put32(b, (hdr_type << 28) | (1u << 22) | (2u << 20) | words);
    put32(b, sid);
    put32(b, static_cast<uint32_t>(sec));
    put32(b, static_cast<uint32_t>(ps >> 32));
    put32(b, static_cast<uint32_t>(ps));
    return b;
}

constexpr uint64_t kSec = 1'700'000'000;
constexpr uint32_t kRate = 2'400'000;
constexpr uint32_t kSamplesPerPacket = 360;

// Timestamp of the k-th sample-index in ps, exact for kRate = 2.4 Msps (1 sample = 1e12/2.4e6 ps, not an integer,
// so round like the sender's clock effectively does).
uint64_t ps_of(uint64_t sample_index) { return (sample_index * 1'000'000'000'000ull + kRate / 2) / kRate; }

// Packet `k` of the stream: samples k*360 .. k*360+359, with I = sample index (mod 30000) + 1, Q = -I.
Bytes data_packet(uint32_t k, uint32_t sid = 7, uint64_t sec_offset = 0) {
    const uint64_t first = static_cast<uint64_t>(k) * kSamplesPerPacket;
    const uint64_t ps = ps_of(first);
    Bytes b = header(1, 5 + kSamplesPerPacket, sid, kSec + sec_offset + ps / 1'000'000'000'000ull,
                     ps % 1'000'000'000'000ull);
    for (uint32_t i = 0; i < kSamplesPerPacket; i++) {
        const auto v = static_cast<int16_t>((first + i) % 30000 + 1);
        for (int16_t s : {v, static_cast<int16_t>(-v)}) {
            b.push_back(static_cast<uint8_t>(static_cast<uint16_t>(s) >> 8));
            b.push_back(static_cast<uint8_t>(s));
        }
    }
    return b;
}

void put_q44_20(Bytes& b, double hz) {
    const auto raw = static_cast<uint64_t>(static_cast<int64_t>(std::llround(hz * (1 << 20))));
    put32(b, static_cast<uint32_t>(raw >> 32));
    put32(b, static_cast<uint32_t>(raw));
}

Bytes context_packet(double rate_hz, double freq_hz, uint32_t sid = 7) {
    Bytes b = header(4, 12, sid, kSec, 0);
    put32(b, (1u << 31) | (1u << 29) | (1u << 27) | (1u << 21));  // change, bandwidth, RF freq, sample rate
    put_q44_20(b, rate_hz);
    put_q44_20(b, freq_hz);
    put_q44_20(b, rate_hz);
    return b;
}

using vrt::Assembler;
Assembler::Kind push(Assembler& a, const Bytes& b) {
    return a.push(std::span<const uint8_t>(b.data(), b.size()));
}

// Sample index encoded in a decoded sample, or -1 for a zero (zero-filled) sample.
int64_t index_of_sample(std::complex<float> s) {
    if (s == std::complex<float>{}) return -1;
    return std::llround(s.real() * 32768.0f) - 1;
}

bool throws(Assembler& a, const Bytes& b) {
    try {
        push(a, b);
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

void test_context_and_ordered_data() {
    Assembler a({});
    CHECK(!a.has_rate());
    CHECK(push(a, data_packet(0)) == Assembler::Kind::Discarded);  // no rate yet
    CHECK(push(a, context_packet(kRate, 1.09e9)) == Assembler::Kind::Context);
    CHECK(a.has_rate());
    CHECK(a.sample_rate_hz() == kRate);
    CHECK(a.rf_freq_hz() && *a.rf_freq_hz() == 1.09e9);

    Samples out;
    for (uint32_t k = 1; k <= 10; k++) CHECK(push(a, data_packet(k)) == Assembler::Kind::Data);
    a.drain(out, true);
    CHECK(out.size() == 10 * kSamplesPerPacket);
    bool contiguous = out.size() == 10 * kSamplesPerPacket;
    for (size_t i = 0; contiguous && i < out.size(); i++) contiguous = index_of_sample(out[i]) == static_cast<int64_t>(i + kSamplesPerPacket) % 30000;
    CHECK(contiguous);
    CHECK(out[0].imag() == -out[0].real());  // I/Q order, sign, and scale: (index+1)/32768
    CHECK(a.stats().zero_filled_samples == 0);
    CHECK(a.stats().discarded_before_rate == 1);
}

void test_explicit_rate_needs_no_context() {
    Assembler a({.sample_rate_hz = kRate});
    CHECK(push(a, data_packet(0)) == Assembler::Kind::Data);
    Samples out;
    a.drain(out, true);
    CHECK(out.size() == kSamplesPerPacket);
    CHECK(push(a, context_packet(kRate, 1.09e9)) == Assembler::Kind::Context);  // agrees: fine
}

void test_reorder_within_window() {
    Assembler a({.sample_rate_hz = kRate, .reorder_window = 8});
    Samples out;
    for (uint32_t k : {2u, 0u, 1u, 4u, 3u, 5u}) push(a, data_packet(k));
    a.drain(out, true);
    CHECK(out.size() == 6 * kSamplesPerPacket);
    bool ordered = out.size() == 6 * kSamplesPerPacket;
    for (size_t i = 0; ordered && i < out.size(); i++) ordered = index_of_sample(out[i]) == static_cast<int64_t>(i) % 30000;
    CHECK(ordered);
    CHECK(a.stats().late_packets == 0);
}

void test_drop_is_zero_filled() {
    Assembler a({.sample_rate_hz = kRate});
    Samples out;
    for (uint32_t k : {0u, 1u, 4u, 5u}) push(a, data_packet(k));  // packets 2 and 3 lost
    a.drain(out, true);
    CHECK(out.size() == 6 * kSamplesPerPacket);
    CHECK(a.stats().zero_filled_samples == 2 * kSamplesPerPacket);
    CHECK(index_of_sample(out[2 * kSamplesPerPacket - 1]) == 2 * kSamplesPerPacket - 1);
    CHECK(index_of_sample(out[2 * kSamplesPerPacket]) == -1);
    CHECK(index_of_sample(out[4 * kSamplesPerPacket - 1]) == -1);
    CHECK(index_of_sample(out[4 * kSamplesPerPacket]) == 4 * kSamplesPerPacket);
}

void test_duplicate_and_late() {
    Assembler a({.sample_rate_hz = kRate, .reorder_window = 2});
    Samples out;
    for (uint32_t k : {0u, 1u, 1u, 2u, 3u, 4u, 5u}) push(a, data_packet(k));
    push(a, data_packet(0));  // arrives long after its window has passed
    a.drain(out, false);      // window of 2 releases the oldest packets, in order
    a.drain(out, true);
    CHECK(out.size() == 6 * kSamplesPerPacket);
    CHECK(a.stats().late_packets == 2);
}

void test_sender_restart() {
    Assembler a({.sample_rate_hz = kRate, .max_gap_s = 1.0});
    Samples out;
    push(a, data_packet(0));
    push(a, data_packet(1));
    push(a, data_packet(0, 7, 3600));  // an hour later: restart, not a 8.6e9-sample zero fill
    a.drain(out, true);
    CHECK(out.size() == 3 * kSamplesPerPacket);
    CHECK(a.stats().restarts == 1);
    CHECK(a.stats().zero_filled_samples == 0);
}

void test_rate_mismatch_throws() {
    Assembler a({.sample_rate_hz = kRate});
    CHECK(throws(a, context_packet(2'000'000, 1.09e9)));
    Assembler b({});
    push(b, context_packet(kRate, 1.09e9));
    CHECK(throws(b, context_packet(2'048'000, 1.09e9)));
}

void test_stream_id_filtering() {
    Assembler a({.sample_rate_hz = kRate});
    CHECK(push(a, data_packet(0, 7)) == Assembler::Kind::Data);  // latches stream 7
    CHECK(push(a, data_packet(1, 9)) == Assembler::Kind::OtherStream);
    Assembler b({.sample_rate_hz = kRate, .stream_id = 9});
    CHECK(push(b, data_packet(0, 7)) == Assembler::Kind::OtherStream);
    CHECK(push(b, data_packet(0, 9)) == Assembler::Kind::Data);
}

void test_invalid_packets() {
    Assembler a({.sample_rate_hz = kRate});
    Bytes good = data_packet(0);
    Bytes truncated(good.begin(), good.end() - 4);
    CHECK(push(a, truncated) == Assembler::Kind::Invalid);  // size field disagrees with datagram length
    Bytes odd(good.begin(), good.end() - 1);
    CHECK(push(a, odd) == Assembler::Kind::Invalid);
    CHECK(push(a, Bytes{1, 2, 3}) == Assembler::Kind::Invalid);
    Bytes with_trailer = good;
    with_trailer[0] |= 0x04;  // trailer-included bit
    CHECK(push(a, with_trailer) == Assembler::Kind::Invalid);
    Bytes wrong_type = good;
    wrong_type[0] = static_cast<uint8_t>((wrong_type[0] & 0x0F) | (2u << 4));  // IF data w/ ext. header type
    CHECK(push(a, wrong_type) == Assembler::Kind::Invalid);
    Bytes short_ctx = header(4, 5, 7, kSec, 0);  // context packet without even a CIF0 word
    CHECK(push(a, short_ctx) == Assembler::Kind::Invalid);
    CHECK(a.stats().invalid_packets == 6);
    CHECK(a.stats().data_packets == 0);
}

}  // namespace

int main() {
    test_context_and_ordered_data();
    test_explicit_rate_needs_no_context();
    test_reorder_within_window();
    test_drop_is_zero_filled();
    test_duplicate_and_late();
    test_sender_restart();
    test_rate_mismatch_throws();
    test_stream_id_filtering();
    test_invalid_packets();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("vita49_stream_test: all tests passed");
    return 0;
}
