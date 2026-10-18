// Simulator back end (see adsb_sim.cpp: Simulator::generate): adds white
// Gaussian receiver noise to the rendered signal and quantizes to interleaved
// int16 IQ. This is most of the simulator's time at high sample rates, so it
// can be split across threads as well as SIMD lanes: sim_finish_range is one
// coro::parallel() part's share, or a piece of it.
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

// out[k - lo] (an (I,Q) int16 pair) = quantize(sig[k] + noise(key, first + k)) for k in [lo, hi), each int16
// big-endian if `big_endian`, else little-endian (the host's). `out` is where sample `lo` goes, so a caller
// can scatter the samples (see Simulator::generate).
export void sim_finish_range(const uniform float sig_i[], const uniform float sig_q[], uniform uint64 first,
                             uniform uint64 key, uniform float sigma, uniform int lo, uniform int hi,
                             uniform bool big_endian, uniform int32 out[]) {
    const uniform uint64 kGamma = 0x9E3779B97F4A7C15ul;
    const uniform float kInv24 = 1.0f / 16777216.0f;
    const uniform float kTwoPi = 6.283185307179586f;
    foreach (i = lo ... hi) {
        uint64 h = mix64(key + (first + (uint64)i) * kGamma);
        // Two disjoint 24-bit uniforms from one 64-bit hash: u1 in (0,1) (never 0, so log is finite; the
        // Gaussian tail is truncated at ~5.9 sigma), u2 in (0,1). Converted through int32, which is exact
        // below 2^24 and much cheaper than from uint32.
        float u1 = ((float)(int32)(h >> 40) + 0.5f) * kInv24;
        float u2 = ((float)(int32)((h >> 16) & 0xFFFFFFul) + 0.5f) * kInv24;
        // Box-Muller: each of I and Q ~ N(0, sigma^2).
        float r = sigma * sqrt(-2.0f * log(u1));
        float s, c;
        sincos(kTwoPi * u2, &s, &c);
        int vi = (int)floor(sig_i[i] + r * c + 0.5f);
        int vq = (int)floor(sig_q[i] + r * s + 0.5f);
        vi = clamp(vi, -32768, 32767);
        vq = clamp(vq, -32768, 32767);
        // One 32-bit store of the (I, Q) int16 pair: I in the low half so it lands at the lower
        // address on a little-endian host (checked host-side). Big-endian swaps the bytes within each half
        // here, saving the caller a pass over the output to do it.
        unsigned int ui = vi & 0xFFFF;
        unsigned int uq = vq & 0xFFFF;
        if (big_endian) {
            ui = ((ui & 0xFF) << 8) | (ui >> 8);
            uq = ((uq & 0xFF) << 8) | (uq >> 8);
        }
        out[i - lo] = (int32)((uq << 16) | ui);
    }
}
