# Reading IQ.
#
# Sections:
#   1. read_iq       raw IQ file -> Vector{ComplexF32}
#   2. read_sigmf    SigMF recording (.sigmf-data + .sigmf-meta) -> samples and sample rate
#   3. debug HDF5    the file `adsb --debug-h5` writes: demod parameters, filter and per-frame IQ
#                    (the functions that need DemodParams are at the end of demod.jl)

using HDF5, JSON

# ----------------------------------------------------------------------------------------------------------
# 1. read_iq
# ----------------------------------------------------------------------------------------------------------

# SigMF datatype -> (component type, byte swap needed on this machine)
function iq_component(datatype::AbstractString)
    m = match(r"^c([fiu])(8|16|32)(_le|_be)?$", datatype)
    m === nothing && error("unsupported IQ datatype $datatype")
    T = Dict("f32" => Float32, "i8" => Int8, "u8" => UInt8, "i16" => Int16, "u16" => UInt16,
             "i32" => Int32, "u32" => UInt32)[m[1]*m[2]]
    big_endian = m[3] == "_be"
    return T, big_endian != (ENDIAN_BOM == 0x01020304)
end

"""
    read_iq(path; datatype = "cf32_le", start = 0, count = nothing)

Reads `count` complex samples (default: to the end of the file) from a raw interleaved IQ file, skipping the
first `start` samples. `datatype` is a SigMF name: `cf32_le` (what `adsb file` reads), `ci16_le`, `ci16_be`,
`cu8` (raw RTL-SDR), `ci8`, ...

Returns `Vector{ComplexF32}`. Integer formats are scaled to roughly ±1 (unsigned ones are centered first, as
`adsb rtlsdr` does for `cu8`). The demodulator's thresholds are relative to the block RMS, so the scale does
not change what it detects.
"""
function read_iq(path; datatype = "cf32_le", start = 0, count = nothing)
    T, swap = iq_component(datatype)
    total = filesize(path) ÷ (2 * sizeof(T))
    n = count === nothing ? total - start : min(count, total - start)
    n >= 0 || error("start $start is past the end of $path ($total samples)")
    raw = Vector{T}(undef, 2n)
    open(path) do io
        seek(io, 2 * sizeof(T) * start)
        read!(io, raw)
    end
    swap && (raw .= bswap.(raw))
    scale, offset = T <: AbstractFloat ? (1f0, 0f0) :
                    T <: Unsigned ? (2f0 / typemax(T), 1f0) : (1f0 / typemax(T), 0f0)
    return [ComplexF32(raw[2i-1] * scale - offset, raw[2i] * scale - offset) for i in 1:n]
end

# ----------------------------------------------------------------------------------------------------------
# 2. read_sigmf
# ----------------------------------------------------------------------------------------------------------

"""
    read_sigmf(path; start = 0, count = nothing)

Reads a SigMF recording. `path` is the `.sigmf-data` file, the `.sigmf-meta` file or their common prefix.
Returns `(x, rate_hz, meta)`: the samples (see `read_iq`), `core:sample_rate` and the parsed metadata.
"""
function read_sigmf(path; start = 0, count = nothing)
    prefix = replace(path, r"\.sigmf(-data|-meta)?$" => "")
    meta = JSON.parsefile(prefix * ".sigmf-meta")
    g = meta["global"]
    x = read_iq(prefix * ".sigmf-data"; datatype = g["core:datatype"], start, count)
    return x, Float64(g["core:sample_rate"]), meta
end

# ----------------------------------------------------------------------------------------------------------
# 3. debug HDF5 (see README.md "Debug HDF5" and src/output/frame_recorder.cpp)
# ----------------------------------------------------------------------------------------------------------
#
#   f = h5open("frames.h5")
#   frame_names(f)                      one group per recorded frame, named by its stream sample index as
#                                       the C++ counts it (from 0)
#   fr = load_frame(f, frame_names(f)[1])
#
# File attributes are the AdsbDemodParams the run used; `filter` is the tap matrix in kernel order.

"Names of the recorded frames, in stream order."
frame_names(f::HDF5.File) = sort(keys(f["frames"]))

"The filter taps the run used, `hk[tap, phase]` in kernel order (see `kernel_taps`)."
h5_filter(f::HDF5.File) = read(f["filter"])

"""
    load_frame(f, name)

One recorded frame as a `Dict`: the group's attributes (`sample_index`, `block_start`, `output_index`,
`envelope_start`, `iq_start`, `level`, `preamble_score`, `confidence`, `df`, `icao`, `crc`, `fixed_bit`,
`payload_raw`, `payload`, ...) plus the datasets:

- `iq`: input samples of the frame's block, `iq[1]` being sample `iq_start` of the block
- `envelope`: the resampled envelope the C++ computed, `envelope[1]` being output `envelope_start`
- `bits`: the 112 sliced bits, before any CRC correction

The file stores indices as the C++ counts them, from 0. They are converted here to Julia's 1-based numbering,
so they agree with `demod_block` and `validate_frame`: `sample_index` and `block_start` (samples of the
stream), `iq_start` (sample of the block), `output_index` and `envelope_start` (outputs of the block), and
`fixed_bit` (bit of the frame, `nothing` if none was corrected). So `fr["sample_index"]` is one more than the
`idx=` that `adsb` logged and than the number in the frame's name.
"""
function load_frame(f::HDF5.File, name)
    g = f["frames"][name]
    fr = Dict{String,Any}(k => read_attribute(g, k) for k in keys(attributes(g)))
    # 0-based in the file -> 1-based. This is the only place HDF5 indices are converted.
    for k in ("sample_index", "block_start", "iq_start", "output_index", "envelope_start")
        fr[k] = Int(fr[k]) + 1
    end
    fr["fixed_bit"] = fr["fixed_bit"] < 0 ? nothing : Int(fr["fixed_bit"]) + 1  # the file has -1 for none
    fr["name"] = name
    fr["iq"] = read(g["iq"])
    fr["envelope"] = read(g["envelope"])
    fr["bits"] = read(g["bits"])
    return fr
end

"""
    frame_table(f)

One line per recorded frame (name, DF, ICAO, CRC status, confidence), for picking one to look at.
"""
function frame_table(f::HDF5.File)
    for name in frame_names(f)
        a(k) = read_attribute(f["frames"][name], k)
        icao = a("icao_known") != 0 ? string(a("icao"), base = 16, pad = 6) : "??????"
        println(name, "  df=", lpad(a("df"), 2), "  icao=", icao, "  crc=", rpad(a("crc"), 9),
                "  confidence=", round(a("confidence"), digits = 1))
    end
end
