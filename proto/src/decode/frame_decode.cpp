#include "decode/frame_decode.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace {

// Mode S / ADS-B CRC-24 remainder, ported bit-for-bit from adsb_detect.jl's
// `crc` function. Operates on a right-justified message of up to 112 bits
// (short 56-bit messages must be pre-shifted so their real content sits in
// the low bits -- see compute_frame_view's `m2` below); the loop always runs
// the same fixed 88 iterations regardless of actual message width; leading
// positions with no real data above the message just shift the generator
// into alignment without touching M, which is a no-op for this style of
// non-shifting LFSR division.
unsigned __int128 mode_s_crc(unsigned __int128 m) {
    unsigned __int128 g = static_cast<unsigned __int128>(0x1fff409) << (112 - 25);
    unsigned __int128 mask0 = static_cast<unsigned __int128>(0x1000000) << (112 - 25);
    unsigned __int128 mask1 = static_cast<unsigned __int128>(0x1FFFFFF) << (112 - 25);
    for (int i = 112; i >= 25; i--) {
        if (m & mask0) {
            m = (~mask1 & m) + ((mask1 & m) ^ g);
        }
        mask0 >>= 1;
        mask1 >>= 1;
        g >>= 1;
    }
    return m;
}

// Frames confirmed by a CRC-valid DF11/17/18 (whose AA field is the ICAO
// address in the clear) accumulate here, so later short replies (DF0/4/5/16/
// 20/21, whose AP field is ICAO XOR parity rather than a plain ICAO) can be
// validated against a previously-seen address -- same two-tier scheme as
// adsb_detect.jl's known_icaos, but built up online as frames arrive rather
// than in a separate first pass, since streaming mode has no "whole batch"
// to pre-scan. A function-local static (rather than a variable threaded
// through both call sites) since only compute_frame_view ever touches it.
std::unordered_set<uint32_t>& known_icaos() {
    static std::unordered_set<uint32_t> icaos;
    return icaos;
}

// Single-bit error correction for 112-bit DF17/18: the CRC is linear, so a frame with bit i flipped
// leaves remainder mode_s_crc(1 << (111 - i)) -- distinct for every i -- and looking the remainder up in
// this table names the bit to flip back. The 5 DF bits are left out: flipping one changes the DF, so a
// frame that reads as DF17/18 can't be one with a DF-field error.
constexpr int kDfBits = 5;
int single_bit_error(unsigned __int128 syndrome) {
    static const auto table = [] {
        std::array<uint32_t, 112> t{};
        for (int i = 0; i < 112; i++) t[i] = static_cast<uint32_t>(mode_s_crc(static_cast<unsigned __int128>(1) << (111 - i)));
        return t;
    }();
    for (int i = kDfBits; i < 112; i++) {
        if (table[i] == syndrome) return i;
    }
    return -1;
}

}  // namespace

DecodedFrameView compute_frame_view(const AdsbFrame& f, double rate_hz) {
    uint64_t idx = f.sample_index;

    unsigned df = static_cast<unsigned>((f.payload >> 107) & 0x1F);
    bool is_short = df == 0 || df == 4 || df == 5 || df == 11;
    // Short (56-bit) replies decode into the top 56 bits of the 112-bit
    // payload (see demod.cpp: kNumBits is always 112); shift the real
    // content down so mode_s_crc sees it right-justified, matching Julia's
    // `m2 = m >> 56`.
    unsigned __int128 m2 = is_short ? (f.payload >> 56) : f.payload;
    unsigned __int128 crc_val = mode_s_crc(m2);
    uint32_t aa_field = static_cast<uint32_t>((f.payload >> 80) & 0xFFFFFF);
    unsigned __int128 payload = f.payload;
    int fixed_bit = -1;

    bool crc_ok = false;
    uint32_t icao = 0;
    bool icao_known = false;
    if (df == 11) {
        crc_ok = crc_val < 63;
        if (crc_ok) {
            icao = aa_field;
            icao_known = true;
            known_icaos().insert(icao);
        }
    } else if (df == 17 || df == 18) {
        crc_ok = crc_val == 0;
        if (!crc_ok) {
            fixed_bit = single_bit_error(crc_val);
            if (fixed_bit >= 0) {
                payload ^= static_cast<unsigned __int128>(1) << (111 - fixed_bit);
                aa_field = static_cast<uint32_t>((payload >> 80) & 0xFFFFFF);
                crc_ok = true;
            }
        }
        if (crc_ok) {
            icao = aa_field;
            icao_known = true;
            known_icaos().insert(icao);
        }
    } else if (df == 0 || df == 4 || df == 5 || df == 16 || df == 20 || df == 21 || df >= 24) {
        // AP field is ICAO XOR parity, not a plain address -- crc_val
        // *is* the ICAO here when it matches a previously confirmed one.
        uint32_t candidate = static_cast<uint32_t>(crc_val & 0xFFFFFF);
        crc_ok = known_icaos().count(candidate) > 0;
        if (crc_ok) {
            icao = candidate;
            icao_known = true;
        }
    }

    const bool crc_checked = df != 19 && df != 22;
    return DecodedFrameView{idx, df, crc_ok, crc_checked, icao_known, icao, payload, fixed_bit, f.num_bits, f.confidence};
}

const char* crc_status(const DecodedFrameView& v) {
    if (!v.crc_checked) return "unchecked";
    return v.fixed_bit >= 0 ? "fixed" : v.crc_ok ? "ok" : "fail";
}

void print_frame_stdout(const DecodedFrameView& v) {
    uint64_t hi = static_cast<uint64_t>(v.payload >> 64);
    uint64_t lo = static_cast<uint64_t>(v.payload & 0xFFFFFFFFFFFFFFFFULL);
    char icao_str[7] = "??????";
    if (v.icao_known) std::snprintf(icao_str, sizeof(icao_str), "%06x", v.icao);

    std::printf("idx=%llu bits=%u confidence=%.1f df=%u icao=%s crc=%s payload=%016llx%016llx\n",
                static_cast<unsigned long long>(v.idx), v.num_bits, v.confidence, v.df, icao_str,
                crc_status(v), static_cast<unsigned long long>(hi), static_cast<unsigned long long>(lo));
}

void to_json(nlohmann::json& j, const DecodedFrameView& v) {
    uint64_t hi = static_cast<uint64_t>(v.payload >> 64);
    uint64_t lo = static_cast<uint64_t>(v.payload & 0xFFFFFFFFFFFFFFFFULL);
    char payload_hex[33];
    std::snprintf(payload_hex, sizeof(payload_hex), "%016llx%016llx", static_cast<unsigned long long>(hi),
                  static_cast<unsigned long long>(lo));
    char icao_str[7] = "";
    if (v.icao_known) std::snprintf(icao_str, sizeof(icao_str), "%06x", v.icao);

    j["type"] = "frame";
    j["idx"] = v.idx;
    j["bits"] = v.num_bits;
    j["confidence"] = v.confidence;
    j["df"] = v.df;
    j["icao"] = v.icao_known ? nlohmann::json(icao_str) : nlohmann::json(nullptr);
    j["crc_ok"] = v.crc_ok;
    j["crc_checked"] = v.crc_checked;
    j["crc_fixed_bit"] = v.fixed_bit >= 0 ? nlohmann::json(v.fixed_bit) : nlohmann::json(nullptr);
    j["payload"] = payload_hex;
}
