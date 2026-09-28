#pragma once

#include <cstdint>

#include <nlohmann/json_fwd.hpp>

#include "dsp/demod.h"

/**
 * @file
 * Mode S frame validation: DF, CRC-24 (with single-bit correction for DF17/18) and ICAO address.
 */

/** One demodulated frame after validation, shared by the stdout printer and the JSON serializer. */
struct DecodedFrameView {
    uint64_t idx;               /**< stream-absolute input sample index of the preamble */
    unsigned df;                /**< downlink format (first 5 bits) */
    bool crc_ok;                /**< passed CRC validation for its DF (after any correction) */
    bool crc_checked;           /**< the DF has a parity rule to check (false for military DF19/22) */
    bool icao_known;            /**< `icao` is valid */
    uint32_t icao;
    unsigned __int128 payload;  /**< 112 bits, with `fixed_bit` (if any) corrected */
    int fixed_bit;              /**< bit (0 = MSB) that CRC error correction flipped, or -1 if none */
    unsigned num_bits;
    float confidence;           /**< AdsbFrame::confidence */
};

/**
 * Validates one demodulated frame.
 *
 * - DF17/18: valid when the CRC remainder is 0; otherwise a single-bit error outside the DF field is
 *   corrected if the remainder matches one.
 * - DF11: valid when the remainder is < 63 (the low bits carry the interrogator ID).
 * - DF0/4/5/16/20/21/24: the AP field is ICAO XOR parity, so the frame is valid when the remainder is an
 *   address previously confirmed by a valid DF11/17/18. DF24 is every DF from 24 to 31: only its first two
 *   bits are the format.
 * - DF19/22 (military) have no general parity rule, so they are unchecked (crc_checked false, crc_ok false).
 * - Every other DF is unassigned, so the frame is corrupt: checked, and failed.
 *
 * Confirmed addresses accumulate in an internal set as frames arrive, so this is not thread-safe.
 *
 * @param f frame from AdsbDemod::exec()
 * @param rate_hz input sample rate (unused)
 */
DecodedFrameView compute_frame_view(const AdsbFrame& f, double rate_hz);

/** "ok", "fixed" (ok after a single-bit correction), "fail" or "unchecked". */
const char* crc_status(const DecodedFrameView& v);

/** Prints one `idx=... df=... icao=... crc=ok|fixed|fail|unchecked payload=...` line to stdout. */
void print_frame_stdout(const DecodedFrameView& v);

/** nlohmann::json conversion (found by ADL): `{"type":"frame", ...}`. */
void to_json(nlohmann::json& j, const DecodedFrameView& v);
