# Reruns AdsbDemod offline on the frames `adsb --debug-h5` recorded (see
# README.md "Debug HDF5"), step by step: resample + envelope from the stored
# IQ and filter, preamble score, bit slice. Needs HDF5.jl.
#
#   julia tools/debug_h5.jl frames.h5     # check every frame
#
# or include() it and work with one frame:
#
#   f = h5open("frames.h5"); fr = load_frame(f, first(keys(f["frames"])))
#   y = envelope(f, fr); bits, mag = slice_bits(f, fr, y)

using HDF5

# One recorded frame: its group's attributes plus iq, envelope and bits.
function load_frame(f, name)
    g = f["frames"][name]
    fr = Dict{String,Any}(k => read_attribute(g, k) for k in keys(attributes(g)))
    fr["iq"] = read(g["iq"])
    fr["envelope"] = read(g["envelope"])
    fr["bits"] = read(g["bits"])
    return fr
end

# Resampled envelope at block-relative outputs `ms` (default: the stored
# envelope's span), exactly as resample_block computes it:
#   P = m*pos_step + pos_offset (fixed point, pos_frac_bits fraction bits)
#   base = P >> pos_frac_bits, phase = ((P & mask) * Np) >> pos_frac_bits
#   y[m] = |sum_j h[j, phase] * x[base - window_lead + j]|
function envelope(f, fr, ms=fr["envelope_start"] .+ (0:length(fr["envelope"])-1))
    a(k) = read_attribute(f, k)
    h = read(f["filter"])  # (taps, num_phases)
    taps, Np = size(h)
    step, offset, fb, lead = a("pos_step"), a("pos_offset"), a("pos_frac_bits"), a("window_lead")
    mask = (Int64(1) << fb) - 1
    x, x0 = fr["iq"], fr["iq_start"]
    map(ms) do m
        P = Int64(m) * step + offset
        base = P >> fb
        phase = ((P & mask) * Np) >> fb
        i = base - lead - x0  # 0-based index into x of the window's first sample
        abs(sum(h[j+1, phase+1] * x[i+j+1] for j in 0:taps-1))
    end
end

# Envelope sample at block-relative output m, given y starting at envelope_start.
at(fr, y, m) = y[m - fr["envelope_start"] + 1]

# Preamble correlation at the frame's position, in units of the block RMS.
function preamble_score(f, fr, y)
    pat, sp = read_attribute(f, "preamble_pattern"), read_attribute(f, "preamble_spacing")
    c = fr["output_index"]
    sum(pat[j+1] * at(fr, y, c + j * sp) for j in 0:15) / fr["level"]
end

# PPM bit slice: bit i compares y at start + i*S and start + i*S + S/2.
# Returns the bits and the slice magnitude in units of the block RMS
# (AdsbFrame::confidence).
function slice_bits(f, fr, y)
    S, nb = read_attribute(f, "samples_per_symbol"), read_attribute(f, "num_bits")
    start = fr["output_index"] + read_attribute(f, "bit_offset")
    d = [at(fr, y, start + i * S) - at(fr, y, start + i * S + S ÷ 2) for i in 0:nb-1]
    return UInt8.(d .> 0), sum(abs, d) / fr["level"]
end

function check(path)
    h5open(path) do f
        names = keys(f["frames"])
        worst, bad = 0.0, 0
        for name in names
            fr = load_frame(f, name)
            y = envelope(f, fr)
            err = maximum(abs.(y .- fr["envelope"])) / maximum(fr["envelope"])
            bits, conf = slice_bits(f, fr, y)
            ps = preamble_score(f, fr, y)
            ok = bits == fr["bits"] && isapprox(conf, fr["confidence"]; rtol=1e-4) &&
                 isapprox(ps, fr["preamble_score"]; rtol=1e-4)
            worst = max(worst, err)
            if !ok || err > 1e-4
                bad += 1
                println("$name: mismatch (envelope rel err $err, confidence $conf vs $(fr["confidence"]), ",
                        "preamble $ps vs $(fr["preamble_score"]), $(sum(bits .!= fr["bits"])) bit(s) differ)")
            end
        end
        crc = [read_attribute(f["frames"][n], "crc") for n in names]
        println("$(length(names)) frames ($(count(==("ok"), crc)) ok, $(count(==("fixed"), crc)) fixed, ",
                "$(count(==("fail"), crc)) fail); $bad mismatched; worst envelope rel err $worst")
    end
end

if abspath(PROGRAM_FILE) == @__FILE__
    check(ARGS[1])
end
