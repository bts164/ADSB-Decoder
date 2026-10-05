# Frame validation: downlink format, CRC-24 and ICAO address (compute_frame_view in src/decode/frame_decode.cpp).
#
# Sections:
#   1. mode_s_crc        CRC-24 remainder
#   2. validate_frame    DF, CRC check (with single-bit correction for DF17/18), ICAO
#   3. validate_frames   a list of frames in stream order, as `adsb` validates them
#   4. printing
#
# Decoding the message contents (callsign, altitude, position, velocity) is not ported; see
# src/decode/aircraft.cpp.

# ----------------------------------------------------------------------------------------------------------
# 1. mode_s_crc
# ----------------------------------------------------------------------------------------------------------

"""
    mode_s_crc(m::UInt128)

Mode S CRC-24 remainder of a right-justified message of up to 112 bits (generator 0x1FFF409). A 56-bit
message must be shifted down first so that its last bit is bit 0.
"""
function mode_s_crc(m::UInt128)
    g = UInt128(0x1fff409) << (112 - 25)
    mask0 = UInt128(0x1000000) << (112 - 25)
    mask1 = UInt128(0x1FFFFFF) << (112 - 25)
    for _ in 112:-1:25
        if m & mask0 != 0
            m = (~mask1 & m) + xor(mask1 & m, g)
        end
        mask0 >>= 1
        mask1 >>= 1
        g >>= 1
    end
    return m
end

# Remainder left by a single flipped bit i (1 = first bit received) of a 112-bit message. The CRC is linear,
# so a frame whose remainder equals one of these has exactly that bit wrong.
const SINGLE_BIT_SYNDROMES = [mode_s_crc(UInt128(1) << (112 - i)) for i in 1:112]

# ----------------------------------------------------------------------------------------------------------
# 2. validate_frame
# ----------------------------------------------------------------------------------------------------------

"""
    validate_frame(payload::UInt128, known_icaos = Set{UInt32}())

Validates one 112-bit payload (the first bit received is the most significant of the 112; a short reply is
the top 56 bits):

- DF17/18: valid when the remainder is 0; otherwise one flipped bit outside the DF field is corrected if the
  remainder matches one.
- DF11: valid when the remainder is below 63 (the low bits carry the interrogator ID).
- DF0/4/5/16/20/21 and 24-31: the parity is XORed with the address, so the frame is valid when the remainder
  is an address in `known_icaos`.
- DF19/22: no general parity rule, so unchecked.
- Any other DF is unassigned: checked and failed.

A valid DF11/17/18 adds its address to `known_icaos`.

Returns `(df, crc, crc_ok, crc_checked, icao_known, icao, payload, fixed_bit, remainder)`. `crc` is `"ok"`,
`"fixed"`, `"fail"` or `"unchecked"`; `payload` has the corrected bit, and `fixed_bit` says which, as an index
into the frame's `bits` (1 = first bit received), or is `nothing` if none was corrected. The C++ and `adsb`'s
outputs number that bit from 0 and use -1 for none. `remainder` is the raw CRC remainder.
"""
function validate_frame(payload::UInt128, known_icaos::Set{UInt32} = Set{UInt32}())
    df = Int((payload >> 107) & 0x1F)
    short = df in (0, 4, 5, 11)
    remainder = mode_s_crc(short ? payload >> 56 : payload)
    aa = UInt32((payload >> 80) & 0xFFFFFF)
    fixed_bit, crc_ok, icao_known, icao = nothing, false, false, UInt32(0)
    if df == 11
        crc_ok = remainder < 63
        if crc_ok
            icao, icao_known = aa, true
            push!(known_icaos, icao)
        end
    elseif df in (17, 18)
        crc_ok = remainder == 0
        if !crc_ok
            # Bits 1-5 are the DF: a frame that reads as DF17/18 can't have an error there.
            i = findfirst(==(remainder), view(SINGLE_BIT_SYNDROMES, 6:112))
            if i !== nothing
                fixed_bit = i + 5  # i indexes the view, which starts at bit 6
                payload = xor(payload, UInt128(1) << (112 - fixed_bit))
                aa = UInt32((payload >> 80) & 0xFFFFFF)
                crc_ok = true
            end
        end
        if crc_ok
            icao, icao_known = aa, true
            push!(known_icaos, icao)
        end
    elseif df in (0, 4, 5, 16, 20, 21) || df >= 24
        candidate = UInt32(remainder & 0xFFFFFF)
        crc_ok = candidate in known_icaos
        crc_ok && ((icao, icao_known) = (candidate, true))
    end
    crc_checked = !(df in (19, 22))
    crc = !crc_checked ? "unchecked" : fixed_bit !== nothing ? "fixed" : crc_ok ? "ok" : "fail"
    return (; df, crc, crc_ok, crc_checked, icao_known, icao, payload, fixed_bit, remainder)
end

# ----------------------------------------------------------------------------------------------------------
# 3. validate_frames
# ----------------------------------------------------------------------------------------------------------

"""
    validate_frames(frames, known_icaos = Set{UInt32}())

Validates frames from `demod_stream` in order, building up `known_icaos` as `adsb` does, so a short reply
validates only after a DF11/17/18 from the same aircraft. Returns each frame merged with its
`validate_frame` result; `payload_raw` keeps the bits as sliced.
"""
validate_frames(frames, known_icaos::Set{UInt32} = Set{UInt32}()) =
    [merge(fr, (; payload_raw = fr.payload), validate_frame(fr.payload, known_icaos)) for fr in frames]

# ----------------------------------------------------------------------------------------------------------
# 4. printing
# ----------------------------------------------------------------------------------------------------------

"112-bit payload as 28 hex digits (the debug HDF5's `payload` format)."
payload_hex(p::UInt128) = string(p, base = 16, pad = 28)

"Parses `payload_hex` output (or the 32-digit form in `adsb`'s frame log lines)."
parse_payload(s::AbstractString) = parse(UInt128, s, base = 16)

"""
    print_frames(frames)

Prints one line per validated frame, in the format of the frame lines `adsb` logs with `--verbosity 2`,
after the frame's position in `frames`.

`idx=` is printed as `adsb` prints it, counting samples from 0, so the lines can be compared with the app's
output: it is `fr.sample_index - 1`. To look up a frame by an `idx` from `adsb`, search for
`sample_index == idx + 1`.
"""
function print_frames(frames)
    for (i, fr) in enumerate(frames)
        icao = fr.icao_known ? string(fr.icao, base = 16, pad = 6) : "??????"
        # - 1: the app's 0-based sample index. This is the only place an index is converted for output.
        println(lpad(i, 4), "  idx=", fr.sample_index - 1, " confidence=", round(fr.confidence, digits = 1),
                " df=", fr.df, " icao=", icao, " crc=", fr.crc, " payload=", payload_hex(fr.payload))
    end
end
