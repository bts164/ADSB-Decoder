# The demodulator, stage by stage, computing what AdsbDemod::exec (src/dsp/demod.cpp, kernels.ispc) computes.
#
# Sections:
#   1. DemodParams        the constants, thresholds and resampling schedule
#   2. resample_at        stage 1: resampled IQ at chosen output positions (abs of it is the envelope)
#   3. preamble_scores    stage 2: preamble correlation
#   4. select_peaks       stage 3: peak picking with an exclusion radius
#   5. slice_bits         stage 4: PPM bit slicing
#   6. demod_block        all stages over one block, keeping every intermediate
#   7. demod_stream       a whole recording, block by block, as `adsb file` processes it
#   8. frame_view         everything about one frame, for plotting (plots.jl)
#   9. debug HDF5         DemodParams and frame_view from an `adsb --debug-h5` file
#
# Index conventions. Indices are ordinary 1-based Julia indices:
#   - "sample"  i: an input IQ sample. Sample i of a recording `x` is x[i].
#   - "output"  m: a sample of the resampled signal, numbered within the block being processed. Output 1 sits
#                  at the block's first sample, and output m is (m - 1) * rate / 12e6 samples after it.
# A block's vectors (z, y, score) are indexed directly by output: y[m] is output m. A frame_view covers only
# part of a block, so it carries the outputs its vectors cover as the range `ms`: v.y[i] is output v.ms[i].
#
# Exceptions, each commented where it occurs:
#   - Offsets and counts start at 0: the chip number in the preamble pattern, the bit number when working out
#     where a bit's samples are, the distance of an output from the start of its block.
#   - The C++ code and its outputs (`idx=` in the log, the debug HDF5 attributes) number samples, outputs and bits
#     from 0. They are converted where they are read (load_frame in iq_io.jl) or printed (print_frames in
#     decode.jl), and nowhere else.

using HDF5

# ----------------------------------------------------------------------------------------------------------
# 1. DemodParams
# ----------------------------------------------------------------------------------------------------------

"8-pulse Mode S preamble as 16 half-symbol chips: +1 where a pulse is expected, -1 where it is not."
const PREAMBLE_PATTERN = Float32[1, -1, 1, -1, -1, -1, -1, 1, -1, 1, -1, -1, -1, -1, -1, -1]

"`adsb`'s block size for file and VITA-49 input."
const DEFAULT_BLOCK_SAMPLES = 128 * 1024

"""
Everything besides the filter taps that fixes what the demodulator computes (`AdsbDemodParams` in demod.h).
Positions are in outputs unless noted.

The resampling schedule is fixed point with `pos_frac_bits` fraction bits: output `m` is
`(m - 1) * pos_step + pos_offset` input samples past the block's first sample. The integer part of that
distance gives the center sample and its fraction picks the phase (see `output_position`).
"""
Base.@kwdef struct DemodParams
    sample_rate_hz::Float64
    samples_per_symbol::Int = 12        # outputs per microsecond
    num_bits::Int = 112                 # always sliced; short replies use the first 56
    preamble_pattern::Vector{Float32} = PREAMBLE_PATTERN
    preamble_spacing::Int               # outputs between pattern chips (half a symbol)
    bit_offset::Int                     # first bit's position after the preamble position (8 us)
    exclusion_radius::Int               # select_peaks radius around an accepted preamble (one frame, 120 us)
    max_lookahead::Int                  # outputs a preamble position needs, itself included
    preamble_score_min::Float32         # threshold, times the block RMS
    slice_magnitude_min::Float32        # threshold, times the block RMS
    pos_frac_bits::Int = 36
    pos_step::Int64
    pos_offset::Int64
    index_offset::Int64                 # gives each output's nearest input sample (center_offset)
    window_lead::Int                    # taps before the center sample in each output's window
end

# A position in samples as fixed point, rounded to nearest (resample_pos_fixed in kernels.ispc).
pos_fixed(pos, frac_bits = 36) = floor(Int64, pos * Float64(Int64(1) << frac_bits) + 0.5)

"""
    DemodParams(hk, sample_rate_hz; preamble_min = 3, slice_mag_min = 112, samples_per_symbol = 12)

Parameters for input at `sample_rate_hz` filtered with `hk` (from `kernel_taps`), with `adsb`'s default
thresholds (`--preamble-min`, `--slice-mag-min`).
"""
function DemodParams(hk::AbstractMatrix, sample_rate_hz; preamble_min = 3, slice_mag_min = 2 * 56,
                     samples_per_symbol = 12)
    S = samples_per_symbol
    taps, Np = size(hk)
    num_bits = 112
    DemodParams(;
        sample_rate_hz, samples_per_symbol = S, num_bits,
        preamble_spacing = S ÷ 2,
        bit_offset = 8S,
        exclusion_radius = (8 + num_bits) * S,
        max_lookahead = 8S + (num_bits - 1) * S + S ÷ 2 + 1,
        preamble_score_min = preamble_min, slice_magnitude_min = slice_mag_min,
        pos_step = pos_fixed((1 / S) * (sample_rate_hz / 1e6)),
        # 1/(2Np) centers each phase's sub-sample interval. Odd taps add 1/2 so the window is centered on the
        # nearest sample; an even-tap window is centered half a sample later by construction.
        pos_offset = pos_fixed((isodd(taps) ? 0.5 : 0.0) + 0.5 / Np),
        index_offset = pos_fixed(0.5 + 0.5 / Np),
        window_lead = (taps - 1) ÷ 2)
end

"Output rate in Hz."
output_rate_hz(P::DemodParams) = P.samples_per_symbol * 1e6

"""
Whole samples from a block's first sample to the input sample nearest output `m`. A frame's `sample_index`
is its block's start plus this for its preamble position.
"""
center_offset(P::DemodParams, m) = (Int64(m - 1) * P.pos_step + P.index_offset) >> P.pos_frac_bits

"""
    output_position(P, m, Np)

`(offset, phase)` of output `m`: `offset` is the whole samples from the block's first sample to the output's
center sample (so 0 for the first sample, being a distance), and `phase` is the column of `hk` to apply.
"""
function output_position(P::DemodParams, m, Np)
    pos = Int64(m - 1) * P.pos_step + P.pos_offset
    mask = (Int64(1) << P.pos_frac_bits) - 1
    # The fraction scaled by Np is a phase number in 0:Np-1; + 1 makes it a column of hk.
    return pos >> P.pos_frac_bits, (((pos & mask) * Np) >> P.pos_frac_bits) + 1
end

# ----------------------------------------------------------------------------------------------------------
# 2. resample_at: resampled IQ (stage 1). The envelope is abs.() of it.
# ----------------------------------------------------------------------------------------------------------

"""
    resample_at(P, hk, x, ms; block_start = 1)

Resampled IQ at outputs `ms` of a block, as `Vector{ComplexF32}`. For output `m` with
`(offset, phase) = output_position(P, m, Np)`:

    z = sum(hk[j, phase] * x[i1 + j - 1] for j in 1:taps),   i1 = block_start + offset - window_lead

`block_start` is the index in `x` of the block's first sample: 1 when `x` starts at the block, the block's
start when `x` is the whole recording. It may be less than 1 when `x` is a window that begins inside the
block (a debug-HDF5 frame). Samples outside `x` count as zero.
"""
function resample_at(P::DemodParams, hk::AbstractMatrix{Float32}, x::AbstractVector{ComplexF32}, ms;
                     block_start = 1)
    taps, Np = size(hk)
    z = Vector{ComplexF32}(undef, length(ms))
    for (o, m) in enumerate(ms)
        offset, phase = output_position(P, m, Np)
        i1 = block_start + offset - P.window_lead  # index in x of the window's first sample
        acc = zero(ComplexF32)
        if i1 >= 1 && i1 + taps - 1 <= length(x)
            @inbounds @simd for j in 1:taps
                acc += hk[j, phase] * x[i1+j-1]
            end
        else
            for j in 1:taps
                1 <= i1 + j - 1 <= length(x) && (acc += hk[j, phase] * x[i1+j-1])
            end
        end
        z[o] = acc
    end
    return z
end

# ----------------------------------------------------------------------------------------------------------
# 3. preamble_scores: preamble correlation (stage 2)
# ----------------------------------------------------------------------------------------------------------

"""
    preamble_scores(P, y, n = <all that fit>)

`score[k]` is the envelope `y` at the 16 chips starting at `k`, `preamble_spacing` apart, each multiplied by
its sign in `preamble_pattern` and summed, for the first `n` positions of `y`. Not normalized: compare against
`preamble_score_min * level`.
"""
function preamble_scores(P::DemodParams, y::AbstractVector{<:Real},
                         n = length(y) - 15 * P.preamble_spacing)
    sp, pat = P.preamble_spacing, P.preamble_pattern
    # j is the chip's offset from the preamble position, so it starts at 0; pat[j+1] is that chip's sign.
    return Float32[sum(y[k+j*sp] * pat[j+1] for j in 0:15) for k in 1:max(n, 0)]
end

# ----------------------------------------------------------------------------------------------------------
# 4. select_peaks: peak picking (stage 3)
# ----------------------------------------------------------------------------------------------------------

"""
    select_peaks(score, min_score, radius)

Greedy peak picking (`select_preamble_peaks` in peak_select.cpp): take the strongest position with
`score >= min_score`, discard everything within `radius` of it, repeat. Returns indices into `score`,
ascending.

One real preamble makes a cluster of above-threshold positions, including sidelobes a few microseconds
either side; this keeps only the strongest.
"""
function select_peaks(score::AbstractVector{<:Real}, min_score, radius)
    above = findall(>=(min_score), score)
    sort!(above, by = i -> (-score[i], i))
    excluded = falses(length(score))
    picks = Int[]
    for i in above
        excluded[i] && continue
        push!(picks, i)
        excluded[max(1, i - radius):min(length(score), i + radius)] .= true
    end
    return sort!(picks)
end

# ----------------------------------------------------------------------------------------------------------
# 5. slice_bits: PPM bit slicing (stage 4)
# ----------------------------------------------------------------------------------------------------------

"""
    slice_bits(P, y, m0, k)

Slices `num_bits` bits for a preamble at output `k`, from the envelope `y` whose first element is output
`m0` (1 for a whole block's envelope). Each bit compares the envelope in the first half of its symbol with
the one half a symbol later: 1 if the first is larger. The first bit's first half is at `k + bit_offset`, and
each later bit is `S` outputs after the one before.

Returns `(bits, d, hi, lo, magnitude, payload)`, the vectors indexed by bit, `bits[1]` being the first
received:
- `bits`: `Vector{UInt8}`
- `d`: per-bit difference (first half minus second half); small `abs(d)` marks a weak bit
- `hi`, `lo`: the outputs sampled for the first and second half of each bit
- `magnitude`: `sum(abs, d)`; divided by the block RMS it is the frame's confidence
- `payload`: the bits as a `UInt128`, the first bit being its most significant of 112
"""
function slice_bits(P::DemodParams, y::AbstractVector{<:Real}, m0, k)
    S = P.samples_per_symbol
    # 0:num_bits-1 is how many symbols each bit is past the first, so it starts at 0.
    hi = k + P.bit_offset .+ S .* (0:P.num_bits-1)
    lo = hi .+ S ÷ 2
    # y[1] is output m0, so output h is y[h - m0 + 1].
    d = Float32[y[h-m0+1] - y[l-m0+1] for (h, l) in zip(hi, lo)]
    bits = UInt8.(d .> 0)
    return (; bits, d, hi, lo, magnitude = sum(abs, d), payload = bits_to_payload(bits))
end

"Packs bits (first received first) into a `UInt128`, the first bit ending up the most significant."
bits_to_payload(bits) = foldl((p, b) -> (p << 1) | UInt128(b), bits; init = UInt128(0))

# ----------------------------------------------------------------------------------------------------------
# 6. demod_block: all stages over one block
# ----------------------------------------------------------------------------------------------------------

"""
    demod_block(P, hk, x, start = 1, n = length(x) - start + 1; first_allowed = 1)

Demodulates the block `x[start:start+n-1]` of the recording `x`, exactly as `AdsbDemod::exec` does, and
returns every intermediate:

- `z`, `y`: resampled IQ and its envelope for outputs `1:n_out` (`y[m]` is output `m`)
- `level`: RMS of the block's own samples, which scales both thresholds
- `score`: preamble correlation for outputs `1:scan_n`, the positions this block owns
- `candidates`: outputs picked as preambles, before the slice-magnitude threshold
- `frames`: the candidates that pass it, each a NamedTuple with `sample_index` (index in `x` of the sample
  nearest the preamble position), `payload`, `confidence`, `preamble_score`, `output_index`, `block_start`,
  `block_n`, `level`
- `next_first_allowed`: the last pick's exclusion zone carried into the next block (see `demod_stream`)

The block reads past both ends for the filter window and for a frame starting near its end, straight from
`x` (zeros beyond it). `first_allowed` is the first output not excluded by the previous block's last pick.
"""
function demod_block(P::DemodParams, hk::AbstractMatrix{Float32}, x::AbstractVector{ComplexF32}, start = 1,
                     n = length(x) - start + 1; first_allowed = 1)
    fb = P.pos_frac_bits
    # This block owns scan_n outputs: those centered before the next block's first sample. Each needs
    # max_lookahead outputs ahead of it; the C++ also rounds the count up to whole batches of 64.
    scan_n = Int(cld((Int64(n) << fb) - P.pos_offset, P.pos_step))
    n_out = cld(scan_n + P.max_lookahead, 64) * 64
    outputs_per_block = n * Float64(Int64(1) << fb) / P.pos_step

    z = resample_at(P, hk, x, 1:n_out; block_start = start)
    y = abs.(z)

    rms = sqrt(sum(abs2, view(x, start:start+n-1)) / Float32(n))
    level = rms > 0 ? rms : 1f0

    score = preamble_scores(P, y, scan_n)

    scan_from = min(first_allowed, scan_n + 1)
    picks = select_peaks(view(score, scan_from:scan_n), P.preamble_score_min * level, P.exclusion_radius)
    candidates = picks .+ (scan_from - 1)  # select_peaks indexes the view; back to outputs of the block

    # The same output is outputs_per_block lower in the next block's numbering.
    next_first_allowed = isempty(candidates) ?
        max(1, ceil(Int, first_allowed - outputs_per_block)) :
        max(1, floor(Int, candidates[end] + P.exclusion_radius - outputs_per_block) + 1)

    frames = NamedTuple[]
    for k in candidates
        s = slice_bits(P, y, 1, k)
        s.magnitude > P.slice_magnitude_min * level || continue
        push!(frames, (; sample_index = start + center_offset(P, k), payload = s.payload,
                       confidence = s.magnitude / level, preamble_score = score[k] / level,
                       output_index = k, block_start = start, block_n = n, level))
    end
    return (; start, n, scan_n, n_out, z, y, level, score, candidates, frames, next_first_allowed)
end

# ----------------------------------------------------------------------------------------------------------
# 7. demod_stream: a whole recording, block by block
# ----------------------------------------------------------------------------------------------------------

"""
    demod_stream(P, hk, x; block_samples = DEFAULT_BLOCK_SAMPLES)

Demodulates the whole recording `x` in blocks, carrying the peak picker's exclusion zone from each block to
the next, and returns every frame (see `demod_block`). With the default block size this is what
`adsb file --iq` computes: the block RMS and the greedy peak picking are both per block, so the block
boundaries matter for reproducing its output.

The frames are not validated: pass them to `validate_frames` (decode.jl) for DF, CRC and ICAO.
"""
function demod_stream(P::DemodParams, hk::AbstractMatrix{Float32}, x::AbstractVector{ComplexF32};
                      block_samples = DEFAULT_BLOCK_SAMPLES)
    frames = NamedTuple[]
    first_allowed = 1
    for start in 1:block_samples:length(x)
        r = demod_block(P, hk, x, start, min(block_samples, length(x) - start + 1); first_allowed)
        append!(frames, r.frames)
        first_allowed = r.next_first_allowed
    end
    return frames
end

# ----------------------------------------------------------------------------------------------------------
# 8. frame_view: everything about one frame, for plotting
# ----------------------------------------------------------------------------------------------------------

"""
    frame_view(P, hk, x, k, level; block_start = 1, ms = <16 us before the preamble to 8 us after the frame>)
    frame_view(P, hk, x, frame)      # a frame from demod_block / demod_stream, x the whole recording

Recomputes every stage around a preamble at output `k` of a block, over the outputs `ms`. `block_start` is
the index in `x` of the block's first sample, as for `resample_at`.

- `ms`, `t_us`: the outputs, and their time in microseconds relative to the preamble position
- `z`, `y`: resampled IQ and envelope at `ms` (`y[i]` is output `ms[i]`; see `envelope_at`)
- `score_ms`, `score`: preamble correlation at every position of `ms` it can be computed for, divided by
  `level` (so directly comparable with `preamble_score_min`)
- `preamble_ms`, `preamble_pattern`: the 16 outputs the correlation at `k` samples, and their signs
- `bits`, `d`, `hi`, `lo`, `magnitude`, `payload`: `slice_bits` at `k`
- `confidence`: `magnitude / level`
- `k`, `level`, `params`

`k` need not be a detected position: pass another to see how a frame would slice from there.
"""
function frame_view(P::DemodParams, hk::AbstractMatrix{Float32}, x::AbstractVector{ComplexF32}, k, level;
                    block_start = 1,
                    ms = k-16*P.samples_per_symbol:k+P.max_lookahead+8*P.samples_per_symbol-1)
    z = resample_at(P, hk, x, ms; block_start)
    y = abs.(z)
    score = preamble_scores(P, y) ./ Float32(level)
    score_ms = first(ms) .+ (0:length(score)-1)
    s = slice_bits(P, y, first(ms), k)
    return (; ms, t_us = (ms .- k) ./ P.samples_per_symbol, z, y, score_ms, score,
            preamble_ms = k .+ P.preamble_spacing .* (0:15), preamble_pattern = P.preamble_pattern,
            s..., confidence = s.magnitude / level, k, level, params = P)
end

frame_view(P::DemodParams, hk::AbstractMatrix{Float32}, x::AbstractVector{ComplexF32}, frame::NamedTuple; kw...) =
    frame_view(P, hk, x, frame.output_index, frame.level; block_start = frame.block_start, kw...)

"The envelope of view `v` at output `m` (`v.y[1]` is output `first(v.ms)`)."
envelope_at(v, m) = v.y[m-first(v.ms)+1]

# ----------------------------------------------------------------------------------------------------------
# 9. debug HDF5: parameters and frame views from an `adsb --debug-h5` file (readers are in iq_io.jl)
# ----------------------------------------------------------------------------------------------------------

"""
    DemodParams(f::HDF5.File)

The parameters the recorded run used. Use with `h5_filter(f)`.
"""
function DemodParams(f::HDF5.File)
    a(k) = read_attribute(f, k)
    DemodParams(;
        sample_rate_hz = a("sample_rate_hz"), samples_per_symbol = a("samples_per_symbol"),
        num_bits = a("num_bits"), preamble_pattern = a("preamble_pattern"),
        preamble_spacing = a("preamble_spacing"), bit_offset = a("bit_offset"),
        exclusion_radius = a("exclusion_radius"), max_lookahead = a("max_lookahead"),
        preamble_score_min = a("preamble_score_min"), slice_magnitude_min = a("slice_magnitude_min"),
        pos_frac_bits = a("pos_frac_bits"), pos_step = a("pos_step"), pos_offset = a("pos_offset"),
        index_offset = a("index_offset"), window_lead = a("window_lead"))
end

"""
    frame_view(f::HDF5.File, fr; k = fr["output_index"])

`frame_view` of a frame from `load_frame`, recomputed from its stored IQ over the span of its stored
envelope. Compare `v.y` with `fr["envelope"]`, `v.bits` with `fr["bits"]`.
"""
function frame_view(f::HDF5.File, fr::AbstractDict; k = fr["output_index"])
    ms = range(fr["envelope_start"], length = length(fr["envelope"]))
    # fr["iq"][1] is sample iq_start of the block, so the block's first sample would be at index 2 - iq_start.
    return frame_view(DemodParams(f), h5_filter(f), fr["iq"], k, fr["level"]; block_start = 2 - fr["iq_start"],
                      ms)
end
