// Simulator back end (see adsb_sim.cpp: Simulator::generate): adds white
// Gaussian receiver noise to the rendered signal and quantizes to interleaved
// int16 IQ. This is most of the simulator's time at high sample rates, so it
// can be split across threads as well as SIMD lanes (see sim_finish).
//
// The noise is counter-based: sample k's I/Q noise is a pure function of
// (key, k), a SplitMix64 hash of the absolute sample index. There is no
// sequential generator state, so every lane computes its own samples
// independently, and the output is identical however the stream is split into
// calls or gangs.

// SplitMix64 output function (Steele/Lea/Flood); with input `key + k*gamma`
// this is exactly SplitMix64's k-th output, which passes BigCrush.
static inline varying uint64 mix64(varying uint64 z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ul;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBul;
    return z ^ (z >> 31);
}

// out[k] (an (I,Q) int16 pair) = quantize(sig[k] + noise(key, first + k)) for k in [lo, hi).
static inline void finish_range(const uniform float sig_i[], const uniform float sig_q[], uniform uint64 first,
                                uniform uint64 key, uniform float sigma, uniform int lo, uniform int hi,
                                uniform int32 out[]) {
    const uniform uint64 kGamma = 0x9E3779B97F4A7C15ul;
    const uniform float kInv24 = 1.0f / 16777216.0f;
    const uniform float kTwoPi = 6.283185307179586f;
    foreach (i = lo ... hi) {
        uint64 h = mix64(key + (first + (uint64)i) * kGamma);
        // Two disjoint 24-bit uniforms from one 64-bit hash: u1 in (0,1) (never 0, so log is finite; the
        // Gaussian tail is truncated at ~5.9 sigma), u2 in (0,1).
        float u1 = ((float)(uint32)(h >> 40) + 0.5f) * kInv24;
        float u2 = ((float)(uint32)((h >> 16) & 0xFFFFFFul) + 0.5f) * kInv24;
        // Box-Muller: each of I and Q ~ N(0, sigma^2).
        float r = sigma * sqrt(-2.0f * log(u1));
        float s, c;
        sincos(kTwoPi * u2, &s, &c);
        int vi = (int)floor(sig_i[i] + r * c + 0.5f);
        int vq = (int)floor(sig_q[i] + r * s + 0.5f);
        vi = clamp(vi, -32768, 32767);
        vq = clamp(vq, -32768, 32767);
        // One aligned 32-bit store of the (I, Q) int16 pair: I in the low half so it lands at the lower
        // address on a little-endian host (checked host-side).
        out[i] = (vq << 16) | (vi & 0xFFFF);
    }
}

// A task is worth its launch only with at least this many samples to do (~13 us of work).
static const uniform int kMinTaskSamples = 8192;

// Each task takes one contiguous chunk of [0, n), a whole number of cache lines of out[].
task void sim_finish_task(const uniform float sig_i[], const uniform float sig_q[], uniform uint64 first,
                          uniform uint64 key, uniform float sigma, uniform int n, uniform int32 out[]) {
    uniform int chunk = ((n + taskCount - 1) / taskCount + 15) & ~15;
    uniform int lo = min(n, (uniform int)taskIndex * chunk);
    uniform int hi = min(n, lo + chunk);
    finish_range(sig_i, sig_q, first, key, sigma, lo, hi, out);
}

// out[k] = quantize(sig[k] + noise(key, first + k)) for k in [0, n), on up to `ntasks` threads (see
// common/tasksys.cpp for the launch/sync runtime); the output does not depend on `ntasks`. With one task
// there is no launch: the loop runs on the calling thread.
export void sim_finish(const uniform float sig_i[], const uniform float sig_q[], uniform uint64 first,
                       uniform uint64 key, uniform float sigma, uniform int n, uniform int32 out[],
                       uniform int ntasks) {
    uniform int nt = min(ntasks, n / kMinTaskSamples);
    if (nt <= 1) {
        finish_range(sig_i, sig_q, first, key, sigma, 0, n, out);
    } else {
        launch[nt] sim_finish_task(sig_i, sig_q, first, key, sigma, n, out);
    }
}
