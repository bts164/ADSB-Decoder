# Exports a PolyphaseFilterBank built in Julia to the raw-file format the
# phase-0 C++/ispc prototype loads (see README.md "Filter coefficient
# source"). Run from the adsb/ directory:
#
#   julia proto/export_filter.jl [M] [Np] [out_prefix]
#
# Writes <out_prefix>.bin (raw Float32 taps, Julia column-major: fastest-
# varying dimension is taps, then phase, then plane) and <out_prefix>.meta
# (plain key=value sidecar describing the shape).

include("../polyphase.jl")

M = length(ARGS) >= 1 ? parse(Int, ARGS[1]) : 33
Np = length(ARGS) >= 2 ? parse(Int, ARGS[2]) : 64
out_prefix = length(ARGS) >= 3 ? ARGS[3] : "proto/filter"

p = PolyphaseFilterBank(Float32, M, Np, 0)

open(out_prefix * ".bin", "w") do io
    write(io, p.h)
end

open(out_prefix * ".meta", "w") do io
    println(io, "taps=", size(p.h, 1))
    println(io, "Np=", size(p.h, 2))
    println(io, "planes=", size(p.h, 3))
    println(io, "dtype=float32")
end

println("wrote ", out_prefix, ".bin (", size(p.h), ") and ", out_prefix, ".meta")
