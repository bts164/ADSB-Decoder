# Plots for debugging frames.
#
# Sections:
#   1. plot_frame     one frame: envelope with the sampled points, preamble correlation, per-bit decisions
#   2. plot_iq        one frame's resampled I and Q
#   3. plot_block     one block: envelope and preamble correlation with the picked peaks
#   4. plot_filter    a filter bank's prototype and frequency response
#
# Uses whichever Plots backend is active. `plotlyjs()` gives zoomable plots, which is what you want for
# looking at individual pulses.

using Plots, FFTW

# ----------------------------------------------------------------------------------------------------------
# 1. plot_frame
# ----------------------------------------------------------------------------------------------------------

"""
    plot_frame(v; bits = nothing, title = "")

Three stacked panels for a `frame_view`, on a shared time axis in microseconds from the preamble position:

1. Envelope (in units of the block RMS) with the points the demodulator sampled:
   - preamble chips: where the pattern expects a pulse, and where it expects none
   - each bit's two samples: the larger one (the value picked for the pulse) and the smaller one
2. Preamble correlation at every position, the threshold, and the position used.
3. Per-bit difference `d` (first half minus second half, in units of the block RMS): positive is a 1,
   negative a 0, and a bar near zero is a weak decision.

`bits`: a reference bit vector (e.g. the CRC-corrected payload from `payload_bits`, or `fr["bits"]` from a
debug HDF5) to mark the bits that differ from it.
"""
function plot_frame(v; bits = nothing, title = "")
    P, k, S = v.params, v.k, v.params.samples_per_symbol
    t(m) = (m .- k) ./ S
    y(ms) = [envelope_at(v, m) for m in ms] ./ v.level

    p1 = plot(v.t_us, v.y ./ v.level; label = "envelope", color = :gray, ylabel = "envelope / RMS", title)
    on = v.preamble_ms[v.preamble_pattern.>0]
    off = v.preamble_ms[v.preamble_pattern.<0]
    scatter!(p1, t(on), y(on); label = "preamble: pulse expected", marker = :utriangle, color = :green)
    scatter!(p1, t(off), y(off); label = "preamble: no pulse expected", marker = :dtriangle, color = :orange)
    picked = ifelse.(v.bits .== 1, v.hi, v.lo)
    other = ifelse.(v.bits .== 1, v.lo, v.hi)
    scatter!(p1, t(picked), y(picked); label = "bit: pulse (larger half)", marker = :circle, color = :blue,
             markersize = 3)
    scatter!(p1, t(other), y(other); label = "bit: other half", marker = :x, color = :red, markersize = 3)

    p2 = plot((v.score_ms .- k) ./ S, v.score; label = "preamble score", color = :purple,
              ylabel = "score / RMS")
    hline!(p2, [P.preamble_score_min]; label = "preamble_score_min", color = :black, linestyle = :dash)
    vline!(p2, [0]; label = "position used", color = :green)

    tb = t(v.hi)
    p3 = bar(tb, v.d ./ v.level; label = "d = first half - second half", color = :steelblue,
             linecolor = :steelblue, bar_width = 0.8, xlabel = "time from preamble position (us)",
             ylabel = "d / RMS")
    if bits !== nothing
        wrong = findall(v.bits .!= bits)
        isempty(wrong) || scatter!(p3, tb[wrong], v.d[wrong] ./ v.level; label = "differs from reference",
                                   marker = :star5, color = :red, markersize = 6)
    end
    return plot(p1, p2, p3; layout = grid(3, 1, heights = [0.5, 0.25, 0.25]), link = :x, size = (1200, 900),
                legend = :outerright, left_margin = 5Plots.mm)
end

"The 112 bits of a payload, first received first, for `plot_frame`'s `bits`."
payload_bits(p::UInt128) = UInt8[(p >> (112 - i)) & 1 for i in 1:112]

# ----------------------------------------------------------------------------------------------------------
# 2. plot_iq
# ----------------------------------------------------------------------------------------------------------

"""
    plot_iq(v; title = "")

Resampled I, Q and envelope of a `frame_view` against time, and the same samples in the IQ plane.
"""
function plot_iq(v; title = "")
    z = v.z ./ v.level
    p1 = plot(v.t_us, real.(z); label = "I", xlabel = "time from preamble position (us)", ylabel = "/ RMS", title)
    plot!(p1, v.t_us, imag.(z); label = "Q")
    plot!(p1, v.t_us, abs.(z); label = "envelope", color = :gray)
    p2 = scatter(real.(z), imag.(z); label = "", markersize = 2, aspect_ratio = :equal, xlabel = "I", ylabel = "Q")
    return plot(p1, p2; layout = grid(1, 2, widths = [0.7, 0.3]), size = (1200, 450))
end

# ----------------------------------------------------------------------------------------------------------
# 3. plot_block
# ----------------------------------------------------------------------------------------------------------

"""
    plot_block(P, r; outputs = 1:r.scan_n)

Envelope and preamble correlation of a `demod_block` result `r` over the block's `outputs`, both in units of
the block RMS, with the threshold, the picked candidates and the ones that became frames.
"""
function plot_block(P::DemodParams, r; outputs = 1:r.scan_n)
    ms = intersect(outputs, 1:r.scan_n)
    p1 = plot(ms, r.y[ms] ./ r.level; label = "envelope", color = :gray, ylabel = "envelope / RMS",
              title = "block at sample $(r.start)")
    p2 = plot(ms, r.score[ms] ./ r.level; label = "preamble score", color = :purple,
              xlabel = "output of the block", ylabel = "score / RMS")
    hline!(p2, [P.preamble_score_min]; label = "preamble_score_min", color = :black, linestyle = :dash)
    cand = filter(in(ms), r.candidates)
    kept = filter(in(ms), [fr.output_index for fr in r.frames])
    scatter!(p2, cand, r.score[cand] ./ r.level; label = "candidates", marker = :x, color = :red)
    scatter!(p2, kept, r.score[kept] ./ r.level; label = "frames", marker = :circle, color = :green)
    return plot(p1, p2; layout = (2, 1), link = :x, size = (1200, 700), legend = :outerright,
                left_margin = 5Plots.mm)
end

# ----------------------------------------------------------------------------------------------------------
# 4. plot_filter
# ----------------------------------------------------------------------------------------------------------

"""
    plot_filter(p, input_rate_hz)

The prototype filter of bank `p` (all phases interleaved) and its magnitude response in dB against
frequency in MHz.
"""
function plot_filter(p::PolyphaseFilterBank, input_rate_hz)
    Np = num_phases(p)
    proto = vec(permutedims(p.h[:, :, 1]))  # proto[(m-1)*Np + phase] = h[m, phase]
    p1 = plot(proto; label = "", xlabel = "prototype tap", ylabel = "coefficient")
    nfft = nextpow(2, 8 * length(proto))
    H = abs.(rfft([proto; zeros(nfft - length(proto))])) ./ Np
    freq_mhz = (0:nfft÷2) .* (input_rate_hz * Np / nfft / 1e6)
    shown = freq_mhz .<= 2 * input_rate_hz / 1e6
    p2 = plot(freq_mhz[shown], 20 .* log10.(max.(H[shown], 1e-12)); label = "", xlabel = "frequency (MHz)",
              ylabel = "response (dB)", ylims = (-120, 5))
    vline!(p2, [input_rate_hz / 2e6]; label = "input Nyquist", color = :black, linestyle = :dash)
    return plot(p1, p2; layout = (2, 1), size = (1000, 700))
end
