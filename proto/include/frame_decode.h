#pragma once

#include <cstdint>

#include <nlohmann/json_fwd.hpp>

#include "demod.h"

// Fields extracted/derived from an AdsbFrame once (idx refinement, df, crc,
// icao), shared by both the stdout printer and the JSON serializer below so
// neither duplicates the CRC/ICAO logic.
struct DecodedFrameView {
    uint64_t idx;
    unsigned df;
    bool crc_ok;
    bool icao_known;
    uint32_t icao;
    unsigned __int128 payload;
    unsigned num_bits;
    float confidence;
};

// Shared by both file mode (base_sample_index=0, exec() sees the whole file
// as one block) and --rtlsdr mode (base_sample_index = running count of
// samples from prior blocks, since each block is its own exec() call with
// block-relative indices -- see rtlsdr_stream.h's run_rtlsdr_stream).
//
// Also maintains (internally) the running set of ICAO addresses confirmed by
// a CRC-valid DF11/17/18, used to validate later short replies whose AP
// field is ICAO XOR parity rather than a plain address. Streaming mode has
// no "whole batch" to pre-scan, so this is built up online as frames arrive;
// only ever called from one thread at a time in a given run (file mode and
// --rtlsdr streaming are mutually exclusive within a single run).
DecodedFrameView compute_frame_view(const AdsbFrame& f, double rate_hz, uint64_t base_sample_index);

void print_frame_stdout(const DecodedFrameView& v);

// nlohmann::json ADL customization point (found via argument-dependent
// lookup) -- lets a DecodedFrameView convert directly via
// nlohmann::json(v)/json j = v, no named conversion function needed.
void to_json(nlohmann::json& j, const DecodedFrameView& v);
