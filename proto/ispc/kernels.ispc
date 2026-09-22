// Phase-0 prototype kernels — see README.md "ispc kernels" and
// "Implementation phasing". All three kernels operate over a whole,
// already-loaded-in-memory block, one SIMD lane per output/candidate
// position, with no serial/recurrence dependency anywhere: resample_block's
// schedule (which input window + filter phase feeds each output sample) is
// a closed-form function of the output index alone, computed independently
// per lane (see its comment below) — an earlier version of this kernel
// used a serial precompute pass instead, which turned out to not even be
// computing the same (correct) thing; see demod.cpp for the derivation.

// Round-half-away-from-zero for uniform double, matching C++'s
// std::lround/std::llround (ispc's round() doesn't cover this case, and
// demod.cpp's ix_center_for mirrors this same convention for the values
// seen here). uniform because the schedule computation below is now
// scalar per output sample — see resample_block.
static inline uniform int iround(uniform double v) { return (uniform int)(v >= 0.0 ? floor(v + 0.5) : ceil(v - 0.5)); }

// Kernel 1: resample + envelope for the whole block.
//
// For output index m, output time t_m = m*ry maps to a continuous input
// sample position ix_float = t_m/rx (taking x[0] as time 0). Adding the
// Np/2 centering offset (matching polyphase.jl's own resample() reference:
// `Np÷2 : 1/rate*Np : ...`, `pi, i = 1+ti%Np, 1+ti÷Np`) and splitting into
// integer/fractional parts gives the center sample index and filter phase.
// This is a closed form — no dependency between output positions.
//
// The schedule (ix_float, base, phase) is computed as uniform scalar
// double per output sample, NOT vectorized across m: with taps=33 vs. a
// 16-wide SIMD gang, batching outputs across lanes would force `phase`
// and `base` to vary per lane, turning the filter's h[]/x[] accesses into
// gathers (an earlier version of this kernel did exactly that). Instead
// the gang parallelizes over the tap dimension for one output at a time
// (see the foreach below): with `phase`/`base` uniform, h[phase*taps+k]
// is an ordinary contiguous vector load, and x[] (interleaved real/imag,
// matching raw SDR I/Q layout) is converted to per-lane real/imag via
// aos_to_soa2 — ispc's shuffle-based AoS->SoA transpose, done once per
// gang-width chunk of taps entirely in registers (the direct analog of a
// CUDA coalesced-load-into-shared-memory-then-transpose, but without an
// explicit shared-memory stage: ispc gangs don't have one, so the
// transpose happens via shuffle instructions on the values already in
// registers). This replaced an earlier planar xr[]/xi[] layout that
// required a host-side deinterleave pass; with `base` uniform per output,
// aos_to_soa2's contiguous read of the interleaved array is exactly as
// cheap as planar's stride-1 load was, so there's no longer a reason to
// pay for that separate pass — see demod.cpp, which now just pads the
// raw interleaved samples (a memcpy) instead of deinterleaving them.
// reduce_add() collapses the per-lane partial sums as before. The
// schedule math itself is cheap relative to the taps-wide filter MAC it
// feeds, so doing it serially per output costs little.
//
// aos_to_soa2 ignores the foreach execution mask and unconditionally
// reads a full gang-width (programCount) of interleaved pairs starting
// at its uniform base, even on the last, partial chunk of the taps loop
// (taps=33 isn't a multiple of the 16-wide gang here) — those extra
// reads land in registers for lanes the mask will discard, so they don't
// affect the result, but the memory they read from must still be valid.
// the caller's guaranteed trailing pad (see adsb_trailing_pad in demod.h)
// covers this.
//
// double (not float) for ix_float/u/base_f/phase is required, not just
// for precision headroom: `float`'s 24-bit mantissa gives an absolute
// precision of x*2^-24 at magnitude x, which needs to stay under
// 1/(2*Np) to round to the correct phase — that fails once ix_float
// exceeds ~2^17 (~131k), reached partway through any file of realistic
// length (this was an actual, confirmed-by-reproduction bug: two
// differently-ordered float32 formulas agreed exactly up to that
// threshold and diverged into noise beyond it). `double`'s 52-bit
// mantissa pushes that threshold to ~2^45, unreachable in practice. The
// taps-loop below stays `float`, matching the filter coefficients and
// input samples and to keep the actual per-lane SIMD math (the dominant
// cost) cheap — only the once-per-output schedule scalar needs double.
//
// An earlier version of this kernel used a serial recurrence to derive
// (ix_center, phase) instead of this closed form (which turned out to
// not even compute the same values: that recurrence's ip was scaled by
// the S=1/ry samples-per-symbol constant instead of by Np, and diverges
// from this closed form almost immediately — see demod.cpp).
//
// `x` is interleaved (real, imag, real, imag, ...) IQ, matching raw SDR
// output. The caller guarantees Mh samples of valid (zero-filled) memory
// immediately before x[0] and Mh+taps+16 immediately after x[n-1] --
// adsb_leading_pad()/adsb_trailing_pad() in demod.h give the exact sizes
// -- so `base + k0` below is always a valid index into `x` with no
// special-casing here, and the host never needs to memcpy the real data
// itself: the guard band is built once, when the caller's buffer is
// populated (see main.cpp), not per exec() call. An earlier version of
// this kernel took an `x_shift` parameter and the host split the m range
// into three (front/interior/tail) pieces with different backing arrays
// to avoid a full O(n) memcpy of the block into a padded copy on every
// call (measured ~7% of total wall time on an 18M-sample file -- see
// README.md's Phase-0 status section); moving the (much smaller, one-
// time) padding cost to wherever the buffer is first populated keeps that
// win while removing all of that per-call complexity.
//
// Thread parallelism: this loop is the dominant cost of the whole pipeline
// (one closed-form schedule + taps-wide MAC per output sample, independent
// across m), so it's split across ispc tasks -- coarse-grained multicore
// parallelism, orthogonal to the SIMD gang above -- rather than left
// single-threaded. `ntasks` is chosen host-side from the hardware thread
// count (see demod.cpp) since ispc code has no way to query that itself;
// each task takes one contiguous chunk of the [0, n_out) range, so
// there's no cross-task sharing beyond the read-only x/h inputs and each
// task's own slice of the y[] output. See src/tasksys.cpp for the
// launch/sync runtime this requires.
task void resample_task(const uniform float x[], const uniform float h[], uniform int taps, uniform int Np,
                         uniform int Mh, uniform double rx, uniform double ry, uniform int n_out, uniform float scale,
                         uniform float y[]) {
    uniform int chunk = (n_out + taskCount - 1) / taskCount;
    uniform int t_start = min(n_out, (uniform int)taskIndex * chunk);
    uniform int t_end = min(n_out, t_start + chunk);
    for (uniform int m = t_start; m < t_end; m++) {
        uniform double ix_float = (uniform double)m * ry / rx;
        uniform double u = ix_float + 0.5;
        uniform double base_f = floor(u);
        uniform int base = (uniform int)base_f;  // = ix_center; see demod.cpp
        uniform int phase = iround((u - base_f) * (uniform double)Np);
        if (phase == Np) {
            phase = 0;
            base += 1;
        }

        float re = 0.f;
        float im = 0.f;
        foreach (k = 0 ... taps) {
            uniform int k0 = extract(k, 0);  // this chunk's start; base+k0 is uniform, see aos_to_soa2 above
            float xr_v, xi_v;
            // aos_to_soa2's uniform float* parameter isn't const-qualified
            // even though it only reads; cast away const to match (x itself
            // stays const from the caller's perspective).
            aos_to_soa2((uniform float * uniform) & x[2 * (base + k0)], &xr_v, &xi_v);
            float hv = h[phase * taps + k];
            re += hv * xr_v;
            im += hv * xi_v;
        }
        // `scale` (== 1/rms, see rms_reduce below) is folded in here rather
        // than applied as a separate pass over x[] before this kernel runs:
        // the FIR filter is linear, so scaling every input sample by a
        // constant and then filtering gives exactly the same result as
        // filtering first and scaling the (much smaller) output — this is
        // the only place normalization costs anything, and it costs one
        // multiply per output sample instead of one per input sample.
        uniform float sre = reduce_add(re) * scale;
        uniform float sim = reduce_add(im) * scale;
        y[m] = sqrt(sre * sre + sim * sim);
    }
}

export void resample_block(const uniform float x[], const uniform float h[], uniform int taps, uniform int Np,
                            uniform int Mh, uniform double rx, uniform double ry, uniform int n_out,
                            uniform float scale, uniform float y[], uniform int ntasks) {
    uniform int nt = max(1, min(n_out, ntasks));
    launch[nt] resample_task(x, h, taps, Np, Mh, rx, ry, n_out, scale, y);
}

// Kernel 0: RMS normalization scale factor for a block, as a task+SIMD
// parallel reduction (sum of squares) over the block's raw, unnormalized
// interleaved samples. Runs ahead of resample_block each call (see
// demod.cpp), one block at a time and independently -- no state carried
// across blocks, see the design note there on why smoothing across
// blocks was deliberately rejected. Only the reduction happens here; the
// tiny finishing scalar math (rms = sqrt(sumsq/n), scale = 1/rms) is left
// to the host, same as resample_block/preamble_scan leave their own
// small scalar follow-up work (peak-picking, bit assembly) to the host.
//
// `partial[]` must have at least `ntasks` elements; the host sums it
// after this call returns (ispc syncs all launched tasks before an
// export function returns, same as resample_block/preamble_scan rely on
// implicitly -- no explicit `sync` needed here either). Accumulated in
// double despite `x` being float32: summing ~1e7+ squared samples in
// float32 risks precision loss the block sizes we care about long-term
// (this kernel exists so blocks can eventually be much smaller than a
// whole file, but must also stay correct for today's whole-file-as-one-
// block usage) -- double removes that concern entirely, matching the
// same precision-headroom reasoning as resample_block's schedule math.
task void sumsq_task(const uniform float x[], uniform int n, uniform float partial[]) {
    uniform int chunk = (n + taskCount - 1) / taskCount;
    uniform int i_start = min(n, (uniform int)taskIndex * chunk);
    uniform int i_end = min(n, i_start + chunk);
    float acc = 0.0;
    foreach (i = i_start ... i_end) {
        uniform int i0 = extract(i, 0);  // this chunk's start; see aos_to_soa2 note on resample_task above
        float re, im;
        aos_to_soa2((uniform float * uniform) & x[2 * i0], &re, &im);
        acc += re * re + im * im;
    }
    partial[taskIndex] = reduce_add(acc);
}

export void sumsq_block(const uniform float x[], uniform int n, uniform float partial[], uniform int ntasks) {
    uniform int nt = max(1, min(n, ntasks));
    launch[nt] sumsq_task(x, n, partial);
}

// Kernel 2: preamble correlation for every candidate start position m in
// [0, scan_limit). Scores each of `num_phases` sub-symbol phase offsets
// against the fixed 16-chip preamble pattern `pat` and keeps the best.
// Caller sizes `scan_limit` so that every m scanned has enough of `y[]`
// ahead of it for both this kernel (needs 99 samples) and the later bit
// slice (needs ~1443) — see demod.cpp.
//
// Thread-parallelized the same way as resample_block (see resample_task):
// profiling after that kernel's own task split showed this one, not
// resample_block, is now the single largest stage in the pipeline (~58%
// of total wall time), since it was never touched by the earlier gather-
// elimination work. Each task takes one contiguous chunk of the m range
// and gangs over it exactly as the original single-task version did.
task void preamble_task(const uniform float y[], uniform int scan_limit, uniform int num_phases,
                         const uniform float pat[], uniform int best_ic[], uniform float best_score[]) {
    uniform int chunk = (scan_limit + taskCount - 1) / taskCount;
    uniform int m_start = min(scan_limit, (uniform int)taskIndex * chunk);
    uniform int m_end = min(scan_limit, m_start + chunk);
    foreach (m = m_start ... m_end) {
        float best = -1.0e30f;
        int bestc = 0;
        for (uniform int c = 0; c < num_phases; c++) {
            float score = 0.f;
            for (uniform int j = 0; j < 16; j++) {
                score += y[m + c + j * 6] * pat[j];
            }
            if (score > best) {
                best = score;
                bestc = c;
            }
        }
        best_score[m] = best;
        best_ic[m] = bestc;
    }
}

export void preamble_scan(const uniform float y[], uniform int scan_limit, uniform int num_phases,
                           const uniform float pat[], uniform int best_ic[], uniform float best_score[],
                           uniform int ntasks) {
    uniform int nt = max(1, min(scan_limit, ntasks));
    launch[nt] preamble_task(y, scan_limit, num_phases, pat, best_ic, best_score);
}

task void preamble_corr_task(const uniform float y[], uniform int n,
                         const uniform float pat[], uniform int n_pat, uniform float c[]) {
    uniform int chunk = (n + taskCount - 1) / taskCount;
    uniform int m_start = min(n, (uniform int)taskIndex * chunk);
    uniform int m_end = min(n, m_start + chunk);
    for (int m = m_start; m < m_end; ++m) {
        float sum = 0;
        foreach (i = 0 ... n_pat) {
            if (m+i < n) {
                sum += y[m+i] * pat[i];
            }
        }
        c[m] = reduce_add(sum);
    }
}

export void preamble_corr(const uniform float y[], uniform int n,
                          const uniform float pat[], uniform int n_pat,
                          uniform float c[], uniform int ntasks) {
    uniform int nt = max(1, min(n, ntasks));
    launch[nt] preamble_corr_task(y, n, pat, n_pat, c);
}

// Kernel 3: PPM bit slicing for a compacted list of `num_candidates`
// positions that already passed the preamble score threshold (host-side
// filter between kernel 2 and this one — see README.md "Design principle:
// parameterize everything" re: preamble_score_min). `bits_out` is
// `num_candidates * num_bits` bytes, `mag_out` is the frame-quality metric
// per candidate.
//
// Gangs over the bit index i for one candidate at a time (mirroring
// resample_block's restructuring) rather than over candidates: `starts[c]`
// is an arbitrary, unrelated-per-candidate position in y[], so a per-lane-
// candidate gang would force every y[] access to gather on a fully
// data-dependent address. With `start` uniform per candidate instead,
// y[start+i*stride]/y[start+i*stride+half] become a fixed-stride access
// (same relative pattern on every call, not data-dependent) and
// bits_out[c*num_bits+i] becomes a genuine contiguous store instead of a
// scatter, since i is exactly the foreach induction variable.
export void slice_scan(const uniform float y[], const uniform int starts[], uniform int stride,
                        uniform int half, uniform int num_bits, uniform int num_candidates,
                        uniform uint8 bits_out[], uniform float mag_out[]) {
    for (uniform int c = 0; c < num_candidates; c++) {
        uniform int start = starts[c];
        float magv = 0.f;
        foreach (i = 0 ... num_bits) {
            float a = y[start + i * stride];
            float b = y[start + i * stride + half];
            float df = a - b;
            bits_out[c * num_bits + i] = (df > 0.f) ? 1 : 0;
            magv += abs(df);
        }
        mag_out[c] = reduce_add(magv);
    }
}
