// Scores the demodulator against the simulator's ground truth: runs adsb_sim (see adsb_sim.h) in-process,
// feeds its IQ through a BlockStitcher and AdsbDemod block by block exactly as `adsb` does (pipeline.h), and
// matches the preamble candidates and output frames against the transmissions the simulator actually sent.
//
// A candidate or frame matches the transmission whose start is nearest, if that is within --tolerance-us.
// Transmissions are also classified as cut by a block boundary (decoded from the stitched guard padding, so
// they should score like clean ones) or overlapping another transmission (garbled on the air); "clean" ones
// are neither, and are what the detection rates are really about.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <argparse/argparse.hpp>

#include "sim/adsb_sim.h"
#include "dsp/demod.h"
#include "dsp/filter_bank.h"
#include "apps/filter_options.h"
#include "decode/frame_decode.h"

namespace {

// Longest Mode S transmission: 8 us preamble + 112 bits at 1 us. AdsbDemod needs this much of the block
// after a preamble to slice it, whatever the frame's length.
constexpr double kLongFrameUs = 8.0 + 112.0;

struct Truth {
    adsb_sim::Transmission tx;
    double end;               // output-sample index just past the last bit
    unsigned __int128 bits;   // frame bits, left-aligned in 112 like AdsbFrame::payload
    bool cut = false;         // a block boundary falls inside [start, start + kLongFrameUs)
    bool overlap = false;     // another transmission is on the air at the same time
    bool candidate = false;   // a preamble candidate matched
    bool output = false;      // an output frame (candidate that passed the slice magnitude) matched
    bool raw_exact = false;   // ... with the transmitted bits, as sliced
    bool fixed = false;       // ... that CRC error correction changed
    bool crc_ok = false;      // ... passing CRC after any correction (as compute_frame_view judges it)
    bool exact = false;       // ... with the transmitted bits after any correction
    int bit_errors = 0;       // ... this many bits wrong as sliced
    double timing_us = 0;     // matched candidate - start
};

unsigned __int128 left_aligned_bits(const adsb_sim::Frame& f) {
    unsigned __int128 p = 0;
    for (unsigned i = 0; i < f.num_bits / 8; i++) p = (p << 8) | f.bytes[i];
    return p << (112 - f.num_bits);
}

int popcount128(unsigned __int128 v) {
    return std::popcount(static_cast<uint64_t>(v)) + std::popcount(static_cast<uint64_t>(v >> 64));
}

// Index of the truth nearest `idx` within `tol` samples, or -1. `truth` is sorted by start.
long nearest(const std::vector<Truth>& truth, double idx, double tol) {
    auto it = std::lower_bound(truth.begin(), truth.end(), idx - tol,
                               [](const Truth& t, double v) { return t.tx.start < v; });
    long best = -1;
    double best_d = tol;
    for (; it != truth.end() && it->tx.start <= idx + tol; ++it) {
        const double d = std::abs(it->tx.start - idx);
        if (d <= best_d) best = it - truth.begin(), best_d = d;
    }
    return best;
}

double pct(size_t a, size_t b) { return b ? 100.0 * static_cast<double>(a) / static_cast<double>(b) : 0.0; }

}  // namespace

int main(int argc, char** argv) {
    argparse::ArgumentParser program("adsb_sim_eval");
    program.add_description("Runs the ADS-B simulator into the demodulator and reports detection statistics "
                            "against the simulator's ground truth.");
    add_filter_options(program);
    program.add_argument("--scenario").required().help("simulator scenario JSON (see scenarios/boston.json)");
    program.add_argument("--rate").scan<'g', double>().default_value(2.4e6).help("sample rate in Hz");
    program.add_argument("--duration").scan<'g', double>().default_value(60.0).help("seconds of signal");
    program.add_argument("--seed").scan<'u', uint64_t>().default_value(uint64_t{1}).help("simulator RNG seed");
    program.add_argument("--snr-db").scan<'g', double>().default_value(30.0).help(
        "pulse power of a full-amplitude aircraft over noise power, per complex sample, in dB");
    program.add_argument("--preamble-min").scan<'g', float>().default_value(3.0f).help(
        "preamble correlation score threshold (as adsb)");
    program.add_argument("--slice-mag-min").scan<'g', float>().default_value(2.0f * 56.0f).help(
        "bit-slice magnitude threshold (as adsb)");
    program.add_argument("--block").scan<'u', unsigned>().default_value(128u * 1024u).help(
        "samples per AdsbDemod::exec() block (default: the file/vita49/rtlsdr sources' 131072)");
    program.add_argument("--tolerance-us").scan<'g', double>().default_value(3.0).help(
        "max distance between a candidate and a transmission's start to count as the same frame");
    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << err.what() << "\n" << program;
        return 1;
    }

    const double rate = program.get<double>("--rate");
    const size_t block = program.get<unsigned>("--block");
    const auto total = static_cast<uint64_t>(program.get<double>("--duration") * rate);
    const double us = rate / 1e6;  // samples per microsecond
    const double tol = program.get<double>("--tolerance-us") * us;

    adsb_sim::Config cfg;
    FilterBank filter;
    try {
        cfg.aircraft = adsb_sim::load_scenario(program.get<std::string>("--scenario"));
        filter = make_filter(program, rate);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    cfg.sample_rate_hz = static_cast<uint32_t>(rate);
    cfg.seed = program.get<uint64_t>("--seed");
    cfg.snr_db = program.get<double>("--snr-db");
    cfg.record_transmissions = true;
    adsb_sim::Simulator sim(cfg);
    AdsbDemod demod(filter, rate, program.get<float>("--preamble-min"), program.get<float>("--slice-mag-min"));

    // Same int16 -> float scaling as the vita49 source (vrt_assembler.cpp).
    const IqPadding pad = AdsbDemod::padding(filter, rate);
    std::vector<int16_t> iq(2 * block);
    std::vector<uint64_t> candidates;
    std::vector<AdsbFrame> frames;
    BlockStitcher stitcher;
    auto process = [&](const IqBlock& b) {
        std::vector<AdsbFrame> out = demod.exec(b);
        frames.insert(frames.end(), out.begin(), out.end());
        candidates.insert(candidates.end(), demod.last_candidates().begin(), demod.last_candidates().end());
    };
    for (uint64_t done = 0; done < total;) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(block, total - done));
        sim.generate(std::span(iq.data(), 2 * n));
        IqBlock b = IqBlock::zeroed(pad, n, done);
        for (size_t i = 0; i < n; i++) b.data()[i] = {iq[2 * i] / 32768.0f, iq[2 * i + 1] / 32768.0f};
        if (auto ready = stitcher.push(std::move(b))) process(*ready);
        done += n;
    }
    if (auto last = stitcher.flush()) process(*last);

    // Ground truth: every transmission that ends inside the generated signal.
    std::vector<Truth> truth;
    for (auto& tx : sim.take_transmissions()) {
        const double end = tx.start + (8.0 + tx.frame.num_bits) * us;
        if (end > static_cast<double>(total)) continue;
        Truth t{tx, end, left_aligned_bits(tx.frame)};
        const auto start_block = static_cast<uint64_t>(tx.start) / block;
        t.cut = static_cast<uint64_t>(tx.start + kLongFrameUs * us) / block != start_block;
        truth.push_back(t);
    }
    std::sort(truth.begin(), truth.end(), [](const Truth& a, const Truth& b) { return a.tx.start < b.tx.start; });
    for (size_t i = 0; i < truth.size(); i++) {
        for (size_t j = i + 1; j < truth.size() && truth[j].tx.start < truth[i].end; j++)
            truth[i].overlap = truth[j].overlap = true;
    }

    size_t false_candidates = 0, duplicate_candidates = 0;
    for (uint64_t c : candidates) {
        const long i = nearest(truth, static_cast<double>(c), tol);
        if (i < 0) {
            false_candidates++;
        } else if (truth[i].candidate) {
            duplicate_candidates++;
        } else {
            truth[i].candidate = true;
            truth[i].timing_us = (static_cast<double>(c) - truth[i].tx.start) / us;
        }
    }

    size_t false_frames = 0, false_frames_crc = 0;
    for (const AdsbFrame& f : frames) {
        const DecodedFrameView v = compute_frame_view(f, rate);
        const long i = nearest(truth, static_cast<double>(f.sample_index), tol);
        if (i < 0 || truth[i].output) {
            false_frames++;
            false_frames_crc += v.crc_ok;
            continue;
        }
        Truth& t = truth[i];
        const unsigned shift = 112 - t.tx.frame.num_bits;
        t.output = true;
        t.bit_errors = popcount128((f.payload ^ t.bits) >> shift);
        t.raw_exact = t.bit_errors == 0;
        t.fixed = v.fixed_bit >= 0;
        t.crc_ok = v.crc_ok;
        t.exact = ((v.payload ^ t.bits) >> shift) == 0;
    }

    // --- report
    struct Tally {
        size_t n = 0, candidate = 0, output = 0, raw_exact = 0, fixed = 0, crc_ok = 0, exact = 0, crc_ok_wrong = 0;
        void add(const Truth& t) {
            n++, candidate += t.candidate, output += t.output, raw_exact += t.raw_exact, fixed += t.fixed;
            crc_ok += t.crc_ok, exact += t.exact, crc_ok_wrong += t.crc_ok && !t.exact;
        }
        void print(const char* label) const {
            std::printf("  %-26s %7zu  %6.2f%%  %6.2f%%  %6.2f%%  %6.2f%%  %6.2f%%  %6.2f%%  %zu\n", label, n,
                        pct(candidate, n), pct(output, n), pct(raw_exact, n), pct(fixed, n), pct(crc_ok, n),
                        pct(exact, n), crc_ok_wrong);
        }
    };
    Tally all, clean, cut, overlap, shorts, longs;
    std::vector<Tally> per_aircraft(cfg.aircraft.size());
    std::array<size_t, 5> err_hist{};  // 1, 2, 3-5, 6-15, 16+ bits wrong (clean, output, not exact)
    size_t clean_no_candidate = 0, clean_rejected = 0;
    double t_sum = 0, t_sq = 0, t_min = 1e9, t_max = -1e9;
    size_t t_n = 0;
    for (const Truth& t : truth) {
        all.add(t);
        if (t.cut) cut.add(t);
        if (t.overlap) overlap.add(t);
        if (t.cut || t.overlap) continue;
        clean.add(t);
        (t.tx.frame.num_bits == 56 ? shorts : longs).add(t);
        per_aircraft[t.tx.aircraft].add(t);
        clean_no_candidate += !t.candidate;
        clean_rejected += t.candidate && !t.output;
        if (t.output && !t.raw_exact) {
            const int e = t.bit_errors;
            err_hist[e == 1 ? 0 : e == 2 ? 1 : e <= 5 ? 2 : e <= 15 ? 3 : 4]++;
        }
        if (t.candidate) {
            t_sum += t.timing_us, t_sq += t.timing_us * t.timing_us, t_n++;
            t_min = std::min(t_min, t.timing_us), t_max = std::max(t_max, t.timing_us);
        }
    }

    const double seconds = static_cast<double>(total) / rate;
    std::printf("%.1f s at %.3f Msps, SNR %.1f dB, seed %llu, block %zu: %zu transmissions\n\n", seconds, rate / 1e6,
                cfg.snr_db, static_cast<unsigned long long>(cfg.seed), block, truth.size());
    std::printf("  %-26s %7s  %7s  %7s  %7s  %7s  %7s  %7s  %s\n", "transmissions", "count", "preamb", "output",
                "raw ok", "fixed", "crc ok", "exact", "crc-ok-but-wrong");
    all.print("all");
    cut.print("cut by block boundary");
    overlap.print("overlapping another");
    clean.print("clean");
    shorts.print("  clean DF11 (56 bit)");
    longs.print("  clean DF17 (112 bit)");
    for (size_t a = 0; a < per_aircraft.size(); a++) {
        char label[64];
        std::snprintf(label, sizeof(label), "  clean %06X amp %.2f", cfg.aircraft[a].icao, cfg.aircraft[a].amplitude);
        per_aircraft[a].print(label);
    }
    std::printf("\nclean misses: %zu no preamble candidate, %zu candidate below --slice-mag-min, "
                "%zu output with bit errors as sliced (1: %zu, 2: %zu, 3-5: %zu, 6-15: %zu, 16+: %zu)\n",
                clean_no_candidate, clean_rejected, clean.output - clean.raw_exact, err_hist[0], err_hist[1],
                err_hist[2], err_hist[3], err_hist[4]);
    std::printf("false preamble candidates: %zu (%.1f/s), duplicate candidates: %zu\n", false_candidates,
                static_cast<double>(false_candidates) / seconds, duplicate_candidates);
    std::printf("false output frames: %zu (%.1f/s), of which pass CRC: %zu\n", false_frames,
                static_cast<double>(false_frames) / seconds, false_frames_crc);
    if (t_n) {
        const double mean = t_sum / static_cast<double>(t_n);
        std::printf("clean candidate timing (candidate - start): mean %+.3f us, sd %.3f us, range [%+.3f, %+.3f] us\n",
                    mean, std::sqrt(std::max(0.0, t_sq / static_cast<double>(t_n) - mean * mean)), t_min, t_max);
    }
    return 0;
}
