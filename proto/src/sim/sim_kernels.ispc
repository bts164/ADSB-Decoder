// Simulator back end (see adsb_sim.cpp: Simulator::generate): adds white
// Gaussian receiver noise to the rendered signal and quantizes to interleaved
// int16 IQ. Single-threaded on purpose: measured, a fork-join task split loses to the SIMD loop at any
// block size this is called with (see Simulator::generate).
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

// out[k] (an (I,Q) int16 pair) = quantize(sig[k] + noise(key, first + k)) for k in [0, n).
export void sim_finish(const uniform float sig_i[], const uniform float sig_q[], uniform uint64 first,
                       uniform uint64 key, uniform float sigma, uniform int n, uniform int32 out[]) {
    const uniform uint64 kGamma = 0x9E3779B97F4A7C15ul;
    const uniform float kInv24 = 1.0f / 16777216.0f;
    const uniform float kTwoPi = 6.283185307179586f;
    foreach (i = 0 ... n) {
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
