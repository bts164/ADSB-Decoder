# Polyphase resampling filter bank.
#
# Sections:
#   1. PolyphaseFilterBank       the original Hann design, with optional derivative planes
#   2. design_filter_bank        the design `adsb` uses by default (window, cutoff, matched pulse)
#   3. load / save               the .bin + .meta files `adsb --filter` reads
#   4. kernel_taps               the tap order the demodulator applies (see demod.jl)
#   5. resample                  plain reference resampler

using DSP, LinearAlgebra, SpecialFunctions

# ----------------------------------------------------------------------------------------------------------
# 1. PolyphaseFilterBank
# ----------------------------------------------------------------------------------------------------------

"""
    PolyphaseFilterBank([T=Float64], M, Np, Nd = 0)

Creates a polyphase filter bank with `M` taps in each of the `Np` banks.
If `Nd > 0` then also adds banks for the first & second derivatives by applying
`2*Nd+1` point centered difference stencils to each of the filter banks individually.

`h` is indexed `h[tap, phase, plane]`. Plane 1 is the filter; planes 2 and 3 are its derivatives.
"""
struct PolyphaseFilterBank{T<:Real}
    h::Array{T,3}
end

function PolyphaseFilterBank(::Type{T}, M, Np, Nd = 0) where T<:Real
    s = -Nd:Nd
    A = [s[i].^d for d in 0:2Nd, i in 1:length(s)]
    window = reshape(
            digitalfilter(Lowpass(1/Np),
            FIRWindow(hanning(Np*M))),
            Np,M)
    h = zeros(T, M + 2Nd, Np, Nd == 0 ? 1 : 3)
    for j in 1:size(h,3)
        b = zeros(T, 2Nd+1)
        b[j] = factorial(j-1)
        c = A \ b
        for pi in 1:Np
            h[:,pi,j] = Np.*xcorr(view(window, pi,:), c)
        end
    end
    PolyphaseFilterBank(h)
end
PolyphaseFilterBank(M, Np, Nd = 0) = PolyphaseFilterBank(Float64, M, Np, Nd)

num_taps(p::PolyphaseFilterBank) = size(p.h, 1)
num_phases(p::PolyphaseFilterBank) = size(p.h, 2)

# ----------------------------------------------------------------------------------------------------------
# 2. design_filter_bank: port of design_filter_bank() in src/dsp/filter_bank.cpp
# ----------------------------------------------------------------------------------------------------------

"Mode S pulse width, for `pulse_width_s` (what `adsb --filter-matched` uses)."
const MODE_S_PULSE_WIDTH_S = 0.5e-6

# Symmetric window of length n at point k (0-based), as window_at() in filter_bank.cpp.
function window_at(window::Symbol, k, n; kaiser_beta = 8.0)
    n == 1 && return 1.0
    x = k / (n - 1)
    if window == :hann
        0.5 * (1 - cos(2π * x))
    elseif window == :hamming
        0.54 - 0.46 * cos(2π * x)
    elseif window == :blackman
        0.42 - 0.5 * cos(2π * x) + 0.08 * cos(4π * x)
    elseif window == :kaiser
        r = 2x - 1
        besseli(0, kaiser_beta * sqrt(max(0.0, 1 - r^2))) / besseli(0, kaiser_beta)
    else
        error("unknown filter window $window (:hann, :hamming, :blackman or :kaiser)")
    end
end

"""
    design_filter_bank([T=Float32], input_rate_hz; taps = 32, num_phases = 64, cutoff_hz = 3e6,
                       window = :hann, kaiser_beta = 8.0, pulse_width_s = 0, output_rate_hz = 12e6)

The resampling filter `adsb` designs at startup. The keywords are its `--filter-*` options with the same
defaults:

- `window`: `:hann`, `:hamming`, `:blackman` or `:kaiser` (with `kaiser_beta`).
- `cutoff_hz`: lowpass cutoff, clamped to the input and output Nyquist frequencies.
- `pulse_width_s`: if positive, the lowpass is convolved with a boxcar this long, which makes the bank a
  matched filter for rectangular pulses of that width. `--filter-matched` is `MODE_S_PULSE_WIDTH_S`.

The prototype is a windowed sinc at `num_phases` times the input rate, scaled to unity DC gain; phase `p`
takes every `num_phases`-th coefficient from `p`. With `:hann`, no pulse and the cutoff at the input Nyquist
this is `PolyphaseFilterBank(T, taps, num_phases)`.
"""
function design_filter_bank(::Type{T}, input_rate_hz; taps = 32, num_phases = 64, cutoff_hz = 3e6,
                            window = :hann, kaiser_beta = 8.0, pulse_width_s = 0.0,
                            output_rate_hz = 12e6) where T<:Real
    cutoff = min(cutoff_hz, input_rate_hz / 2, output_rate_hz / 2)
    n = taps * num_phases
    # Boxcar length in prototype samples. The lowpass is pulse - 1 shorter than n, so their convolution is
    # exactly n long and has the same center.
    pulse = pulse_width_s > 0 ? max(1, round(Int, pulse_width_s * input_rate_hz * num_phases)) : 1
    pulse < n || error("filter pulse width is longer than the filter")
    n_lp = n - pulse + 1

    w = 2 * cutoff / (input_rate_hz * num_phases)  # cutoff as a fraction of the prototype rate's Nyquist
    lp = [w * sinc(w * (k - (n_lp - 1) / 2)) * window_at(window, k, n_lp; kaiser_beta) for k in 0:n_lp-1]
    proto = zeros(n)
    for k in 1:n_lp, j in 0:pulse-1
        proto[k+j] += lp[k]
    end
    proto .*= num_phases / sum(proto)  # every phase gets roughly unity DC gain

    h = Array{T,3}(undef, taps, num_phases, 1)
    for p in 1:num_phases, m in 1:taps
        h[m, p, 1] = proto[(m-1)*num_phases+p]
    end
    return PolyphaseFilterBank(h)
end
design_filter_bank(input_rate_hz; kw...) = design_filter_bank(Float32, input_rate_hz; kw...)

# ----------------------------------------------------------------------------------------------------------
# 3. load / save: the files `adsb --filter <bin> --filter-meta <meta>` reads
# ----------------------------------------------------------------------------------------------------------

"""
    save_filter_bank(p, prefix)

Writes `prefix.bin` (raw Float32, column-major `h[tap, phase, plane]`) and `prefix.meta` (`key=value` lines).
"""
function save_filter_bank(p::PolyphaseFilterBank, prefix)
    write(prefix * ".bin", Float32.(p.h))
    open(prefix * ".meta", "w") do io
        println(io, "taps=", size(p.h, 1))
        println(io, "Np=", size(p.h, 2))
        println(io, "planes=", size(p.h, 3))
        println(io, "dtype=float32")
    end
    return prefix
end

"""
    load_filter_bank(prefix)

Reads the `prefix.bin` / `prefix.meta` pair written by `save_filter_bank`.
"""
function load_filter_bank(prefix)
    kv = Dict(String.(split(l, '=', limit = 2)) for l in eachline(prefix * ".meta") if occursin('=', l))
    get(kv, "dtype", "float32") == "float32" || error("unsupported filter dtype $(kv["dtype"])")
    h = Array{Float32,3}(undef, parse(Int, kv["taps"]), parse(Int, kv["Np"]), parse(Int, get(kv, "planes", "1")))
    read!(prefix * ".bin", h)
    return PolyphaseFilterBank(h)
end

# ----------------------------------------------------------------------------------------------------------
# 4. kernel_taps: the tap order the demodulator applies
# ----------------------------------------------------------------------------------------------------------

"""
    kernel_taps(p)

Plane 1 of `p` as a `Float32` matrix `hk[tap, phase]` with each phase's taps reversed, so that they apply
against increasing sample index: output = `sum(hk[j, phase] * x[first + j - 1])`. This is `FilterBank::coeffs`
in the C++ code and the `filter` dataset of a `--debug-h5` file.
"""
kernel_taps(p::PolyphaseFilterBank) = Float32.(reverse(view(p.h, :, :, 1), dims = 1))

# ----------------------------------------------------------------------------------------------------------
# 5. resample: plain reference resampler
# ----------------------------------------------------------------------------------------------------------

"""
    resample(p, x, rate)

Resamples `x` by `rate` (output rate / input rate) with every plane of `p`, zero-extending `x` at both ends.
Returns a `length × planes` matrix. Works for an odd number of taps only.

This is the simple floating-point reference. The demodulator's own schedule, which matches the C++ code
sample for sample and handles even tap counts, is `resample_at` in demod.jl.
"""
function resample(p::PolyphaseFilterBank, x::AbstractVector{T}, rate::Real) where T<:Number
    N = length(x)
    Mh = size(p.h,1)÷2
    Np = size(p.h,2)
    ts = Np÷2:Np/rate:N*Np-Np÷2
    out = zeros(T, length(ts), size(p.h,3))
    for (oi,t) in enumerate(ts)
        ti = Int(round(t)) - 1
        pi, i = 1 + ti % Np, 1 + ti ÷ Np
        for k in 1:size(p.h,3)
            for m in -Mh:Mh
                if 1 <= i + m <= N
                    out[oi,k] += p.h[Mh-m+1,pi,k] * x[i+m]
                end
            end
        end
    end
    return out
end
