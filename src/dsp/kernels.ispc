// Phase-0 prototype kernels — see README.md "ispc kernels" and
// "Implementation phasing". All three kernels operate over a whole,
// already-loaded-in-memory block, one SIMD lane per output/candidate
// position, with no serial/recurrence dependency anywhere: resample_block's
// schedule (which input window + filter phase feeds each output sample) is
// a closed-form function of the output index alone, computed independently
// per lane (see its comment below) — an earlier version of this kernel
// used a serial precompute pass instead, which turned out to not even be
// computing the same (correct) thing; see demod.cpp for the derivation.

// Kernel 1: resample + envelope for the whole block.
//
// For output index m, output time t_m = m*ry maps to a continuous input
// sample position t_m/rx (taking x[0] as time 0). Measured in filter-phase
// units (1/Np of a sample) and adding the Np/2 centering offset (matching
// polyphase.jl's own resample() reference: `Np÷2 : 1/rate*Np : ...`,
// `pi, i = 1+ti%Np, 1+ti÷Np`), that is T = round(m*step + Np/2) with
// step = ry/rx*Np, and T splits into the center sample index (T / Np) and
// filter phase (T % Np). Equivalently, with the position measured in
// samples, pos = m*ry/rx + 1/2 + 1/(2*Np): base = floor(pos) and
// phase = floor(Np * frac(pos)). The kernel uses that second form, in
// fixed point (see resample_pos_fixed below), which needs only an integer
// multiply-add, shift and mask per output -- no divide, for any Np. This is
// a closed form: no dependency between output positions. The kernel itself
// just applies `offset`; that 1/2 is for odd tap counts, whose window is
// centered on base. An even-tap window is centered on base + 1/2 instead,
// so the host (demod.cpp pos_offset) drops the 1/2 for those.
//
// The schedule (base, phase) is computed as uniform scalar int64 per
// output sample, NOT vectorized across m: batching outputs
// across lanes would force `phase` and `base` to vary per lane, turning
// the filter's h[]/x[] accesses into gathers (an earlier version of this
// kernel did exactly that). Instead the gang parallelizes over the tap
// dimension for one output at a time (see the foreach below).
//
// `x` is interleaved (real, imag, real, imag, ...) IQ, matching raw SDR
// output, and is consumed as-is: the taps table `h` has every tap stored
// twice (h0, h0, h1, h1, ...; see FilterBank::kernel_coeffs), so one
// contiguous load of h and one of x line up lane for lane, even lanes
// carrying the real products and odd lanes the imaginary ones. No
// deinterleave/shuffle is needed; the per-lane products accumulate in a
// single vector, and at the end the even and odd lanes are summed
// separately (foreach chunks start at multiples of programCount, an even
// number, so lane parity is just programIndex & 1). Each row of h is
// `stride` floats: zero-padded so it is a multiple of the widest gang and
// the foreach below has no partial final chunk on any target up to 16
// lanes (it stays correct, just with a masked tail, on wider ones). The
// padding taps are zero, so the extra x samples they multiply don't
// matter, but they must be readable: the caller's trailing pad covers it.
// The schedule math itself is cheap relative to the stride-wide filter
// MAC it feeds, so doing it serially per output costs little -- as long as
// it has no divide: a 64-bit `T / Np` per output measured ~14% of this
// kernel's time.
//
// `float` for the position is not enough: its 24-bit mantissa gives an
// absolute precision of x*2^-24 at magnitude x, which needs to stay under
// 1/(2*Np) to pick the correct phase -- that fails once the position
// exceeds ~2^17 (~131k) samples, i.e. within one default-size block (this
// was an actual, confirmed-by-reproduction bug). Fixed point keeps a
// constant 2^-kPosFracBits of a sample everywhere instead.
//
// An earlier version of this kernel used a serial recurrence to derive
// (ix_center, phase) instead of this closed form (which turned out to
// not even compute the same values: that recurrence's ip was scaled by
// the S=1/ry samples-per-symbol constant instead of by Np, and diverges
// from this closed form almost immediately — see demod.cpp).
//
// The caller guarantees every sample the tap window touches is readable (AdsbDemod::padding() sizes the
// block's guard padding for it), so there is no edge handling here, and that n_out is a multiple of
// programCount, so every batch of outputs below is full.
//
// Thread parallelism: this loop is the dominant cost of the whole pipeline
// (one closed-form schedule + taps-wide MAC per output sample, independent
// across m), so it's split across ispc tasks -- coarse-grained multicore
// parallelism, orthogonal to the SIMD gang above -- rather than left
// single-threaded. `ntasks` is chosen host-side from the hardware thread
// count (see demod.cpp) since ispc code has no way to query that itself;
// each task takes one contiguous chunk of the [0, n_out) range, so
// there's no cross-task sharing beyond the read-only x/h inputs and each
// task's own slice of the y[] output. See common/tasksys.cpp for the
// launch/sync runtime this requires.
// Output positions in input samples, as fixed point with kPosFracBits fractional bits: output m's position is
// P = m * step + offset (see resample_block). 36 bits keeps the rounding error of `step`, accumulated over m,
// under ~1e-6 sample (~1e-4 of a phase at Np=64) for m up to ~131k outputs, and m * step stays in int64 for
// positions up to 2^27 samples.
static const uniform int kPosFracBits = 36;

// Converts a position in samples to the fixed point above (round to nearest; pos >= 0). The host uses this for
// resample_block's `step` and `offset`, so it and the kernel agree on the exact values.
export uniform int64 resample_pos_fixed(uniform double pos) {
    return (uniform int64)floor(pos * (uniform double)((uniform int64)1 << kPosFracBits) + 0.5);
}

// Center input sample index (`base` below) of output m. The host uses this to report frame sample indices.
export uniform int resample_center_index(uniform int m, uniform int64 step, uniform int64 offset) {
    return (uniform int)(((uniform int64)m * step + offset) >> kPosFracBits);
}

task void resample_task(const uniform float x[], const uniform float h[], uniform int stride, uniform int Np,
                         uniform int64 step, uniform int64 offset, uniform int n_out, uniform float y[],
                         const uniform float samples[], uniform int n, uniform float sumsq[]) {
    // Whole batches per task, so with n_out a multiple of programCount every task's range is too.
    uniform int chunk = ((n_out + taskCount - 1) / taskCount + programCount - 1) / programCount * programCount;
    uniform int t_start = min(n_out, (uniform int)taskIndex * chunk);
    uniform int t_end = min(n_out, t_start + chunk);
    const uniform int64 frac_mask = ((uniform int64)1 << kPosFracBits) - 1;

    // This task's share of the block's sum of squares (see resample_block): the input samples from its first
    // output's center sample up to (not including) the next task's, accumulated a batch at a time alongside
    // the filter so those loads overlap its math. Every task's range meets the next's, the first starts at
    // sample 0 (base(0) = 0) and the last is extended to n, so together they cover [0, n) exactly once. Sum of
    // re^2 + im^2 over samples is just the sum of x^2 over the interleaved floats.
    float ss = 0.f;
    uniform int s_next = clamp(resample_center_index(t_start, step, offset), 0, n);

    for (uniform int m0 = t_start; m0 < t_end; m0 += programCount) {
        uniform int m1 = m0 + programCount;
        uniform int s_hi = m1 == n_out ? n : clamp(resample_center_index(m1, step, offset), 0, n);
        foreach (k = 2 * s_next ... 2 * s_hi) {
            ss += samples[k] * samples[k];
        }
        s_next = s_hi;

        // One tap-parallel accumulator per output in this batch: even lanes hold I partial sums, odd lanes Q.
        float acc[programCount];
        for (uniform int i = 0; i < programCount; i++) {
            uniform int64 P = (uniform int64)(m0 + i) * step + offset;
            uniform int base = (uniform int)(P >> kPosFracBits);  // = resample_center_index(m0 + i, ...)
            uniform int phase = (uniform int)(((P & frac_mask) * Np) >> kPosFracBits);

            const uniform float* uniform xw = &x[2 * base];
            const uniform float* uniform hw = &h[phase * stride];
            float a = 0.f;
            foreach (j = 0 ... stride) {
                a += hw[j] * xw[j];
            }
            acc[i] = a;
        }

        // Transpose-and-sum the batch: each level halves both the number of vectors and the lanes each output
        // is spread over, stopping at (I, Q) lane pairs. Pairing vector k with k + nv/2 at the level that splits
        // on lane bit s puts output m0+i in lane i (times 2 for the I/Q pair) once all levels are done.
        uniform int nv = programCount;
        for (uniform int s = programCount / 2; s >= 2; s /= 2) {
            bool hi = (programIndex & s) != 0;
            for (uniform int k = 0; k < nv / 2; k++) {
                float a = acc[k];
                float b = acc[k + nv / 2];
                acc[k] = (hi ? b : a) + shuffle(hi ? a : b, programIndex ^ s);
            }
            nv /= 2;
        }

        // Last level: combine the two remaining (I, Q)-pair vectors into |z| for every output.
        float p0 = acc[0];
        float p1 = acc[1];
        bool odd = (programIndex & 1) != 0;
        float re = odd ? shuffle(p1, programIndex ^ 1) : p0;
        float im = odd ? p1 : shuffle(p0, programIndex ^ 1);
        float mag = sqrt(re * re + im * im);

        y[m0 + programIndex] = mag;
    }
    sumsq[taskIndex] = reduce_add(ss);
}

// `step` = resample_pos_fixed(ry / rx) and `offset` = resample_pos_fixed(0.5 + 0.5 / Np); see the derivation at
// the top of this file. `n_out` must be a multiple of programCount.
// Also sums the squares of the block's n raw input samples `samples` (the same block as `x`, without the
// leading-guard offset), one partial per task into `sumsq[]`, which must have at least `ntasks` elements; the
// host adds them up for the block's RMS. That normalization is never applied to y[] itself: the filter and
// |z| are linear in the input's scale, so the host scales its thresholds by rms instead (see demod.cpp). Doing
// it in this kernel saves a separate launch and pass over the input.
export void resample_block(const uniform float x[], const uniform float h[], uniform int stride, uniform int Np,
                            uniform int64 step, uniform int64 offset, uniform int n_out, uniform float y[],
                            const uniform float samples[], uniform int n, uniform float sumsq[],
                            uniform int ntasks) {
    uniform int nt = max(1, min(n_out, ntasks));
    launch[nt] resample_task(x, h, stride, Np, step, offset, n_out, y, samples, n, sumsq);
}

// Kernel 2: preamble correlation score[k] = sum_j y[k + j*half_sym] * pat[j]
// for every candidate preamble start k in [0, n), one lane per k: `pat` is
// the 16-chip (8 us) preamble pattern at half-symbol spacing. Caller sizes
// `n` so every k scanned has enough of `y[]` ahead of it for both this
// kernel (15*half_sym + 1 samples) and the later bit slice -- see
// demod.cpp.
//
// Each start position is scored exactly once, with no search over nearby
// sub-chip phases: choosing the best start in a neighborhood is the host's
// peak selection (peak_select.h), whose exclusion radius is far wider than
// any such phase search. Thread-parallelized the same way as
// resample_block: each task takes one contiguous chunk of the k range.
task void preamble_task(const uniform float y[], uniform int n, uniform int half_sym, const uniform float pat[],
                         uniform float score[]) {
    uniform int chunk = (n + taskCount - 1) / taskCount;
    uniform int k_start = min(n, (uniform int)taskIndex * chunk);
    uniform int k_end = min(n, k_start + chunk);
    foreach (k = k_start ... k_end) {
        float s = 0.f;
        for (uniform int j = 0; j < 16; j++) {
            s += y[k + j * half_sym] * pat[j];
        }
        score[k] = s;
    }
}

export void preamble_scan(const uniform float y[], uniform int n, uniform int half_sym, const uniform float pat[],
                           uniform float score[], uniform int ntasks) {
    uniform int nt = max(1, min(n, ntasks));
    launch[nt] preamble_task(y, n, half_sym, pat, score);
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
