#include "sim/adsb_sim.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <fstream>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>
#include <xtensor/xmath.hpp>
#include <xtensor/xtensor.hpp>

#include "common/constants.h"
#include "sim_kernels_ispc.h"

namespace adsb_sim {

namespace {

// --- Message encoding ---------------------------------------------------

void put_bits(std::array<uint8_t, 14>& bytes, unsigned pos, unsigned n, uint64_t v) {
    for (unsigned i = 0; i < n; i++) {
        const unsigned bit = static_cast<unsigned>((v >> (n - 1 - i)) & 1);
        bytes[(pos + i) / 8] |= static_cast<uint8_t>(bit << (7 - ((pos + i) % 8)));
    }
}

constexpr uint8_t kCapability = 5;  // CA=5: level 2+ transponder, airborne

Frame make_df17(uint32_t icao, uint64_t me56) {
    Frame f;
    f.num_bits = 112;
    put_bits(f.bytes, 0, 5, 17);
    put_bits(f.bytes, 5, 3, kCapability);
    put_bits(f.bytes, 8, 24, icao & 0xFFFFFF);
    put_bits(f.bytes, 32, 56, me56);
    put_bits(f.bytes, 88, 24, crc24(f.bytes.data(), 11));
    return f;
}

double pmod(double a, double b) {
    const double r = std::fmod(a, b);
    return r < 0 ? r + b : r;
}

// Number of CPR longitude zones at `lat` (DO-260 NL(lat), NZ=15). Written
// independently of aircraft.cpp's decoder-side copy so a test round-trip
// through the real decoder actually cross-checks the two.
int cpr_nl(double lat) {
    lat = std::abs(lat);
    if (lat == 0) return 59;
    if (lat == 87.0) return 2;
    if (lat > 87.0) return 1;
    const double a = 1 - std::cos(kPi / 30.0);
    const double b = std::cos(kPi / 180.0 * lat);
    return static_cast<int>(std::floor(2 * kPi / std::acos(1 - a / (b * b))));
}

// --- Deterministic RNG --------------------------------------------------
//
// Hand-rolled rather than <random>: the standard distributions are
// implementation-defined, so std::normal_distribution would not give the same
// stream across standard libraries for the same seed.

uint64_t splitmix64(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

class Rng {
public:
    // Independent stream per (seed, stream) pair.
    Rng(uint64_t seed, uint64_t stream) {
        uint64_t sm = seed ^ (stream * 0xD1B54A32D192ED03ull);
        for (auto& w : m_s) w = splitmix64(sm);
    }

    uint64_t next() {  // xoshiro256**
        const uint64_t result = rotl(m_s[1] * 5, 7) * 9;
        const uint64_t t = m_s[1] << 17;
        m_s[2] ^= m_s[0];
        m_s[3] ^= m_s[1];
        m_s[1] ^= m_s[2];
        m_s[0] ^= m_s[3];
        m_s[2] ^= t;
        m_s[3] = rotl(m_s[3], 45);
        return result;
    }

    double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }  // [0, 1)

    // One complex Gaussian sample, each of I and Q ~ N(0, sigma^2) (Box-Muller).
    void gaussian_pair(double sigma, double& i, double& q) {
        const double r = sigma * std::sqrt(-2.0 * std::log(1.0 - uniform()));
        const double theta = 2.0 * kPi * uniform();
        i = r * std::cos(theta);
        q = r * std::sin(theta);
    }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t m_s[4];
};

// --- Scenario ------------------------------------------------------------

constexpr double kSecondsPerHour = 3600.0;
constexpr double kArcminPerDegree = 60.0;
constexpr int kMaxAltFt = 50175;  // top of the Q=1 25 ft altitude encoding

// Flat-earth motion at constant velocity -- plenty for a signal source.
struct Kinematics {
    double lat_deg, lon_deg;
    int alt_ft;
    double east_kt, north_kt;
};

Kinematics kinematics_at(const AircraftSpec& a, double t_s) {
    const double track = a.track_deg * kPi / 180.0;
    const double east_kt = a.ground_speed_kt * std::sin(track);
    const double north_kt = a.ground_speed_kt * std::cos(track);
    // 1 kt = 1 nmi/h and 1 nmi = 1 arcminute of latitude.
    const double deg_per_s_per_kt = 1.0 / (kSecondsPerHour * kArcminPerDegree);
    const double lat = a.lat_deg + north_kt * t_s * deg_per_s_per_kt;
    const double lon = a.lon_deg + east_kt * t_s * deg_per_s_per_kt / std::cos(a.lat_deg * kPi / 180.0);
    const int alt = std::clamp(a.alt_ft + static_cast<int>(std::lround(a.vrate_fpm * t_s / 60.0)), -1000, kMaxAltFt);
    return {lat, lon, alt, east_kt, north_kt};
}

enum class Kind { Squitter, Identification, Position, Velocity };

struct KindSchedule {
    Kind kind;
    double period_s;
    double max_first_offset_s;
};
// Nominal DO-260 ADS-B rates, with the first transmission offset randomly so
// aircraft don't all key up at once. The identification's real period (5 s)
// would leave a fresh receiver waiting for its callsign, so its first
// transmission is pulled forward.
constexpr KindSchedule kSchedules[] = {
    {Kind::Squitter, 1.0, 1.0},
    {Kind::Identification, 5.0, 1.0},
    {Kind::Position, 0.5, 0.5},
    {Kind::Velocity, 0.5, 0.5},
};

constexpr unsigned kPreambleChips = 16;
constexpr uint64_t kChipsPerSecond = 2'000'000;  // 0.5 us chips
constexpr unsigned kMaxChips = kPreambleChips + 2 * 112;
// generate() renders at most this many output samples at a time.
constexpr size_t kSubBlockSamples = 32768;
constexpr double kBaseAmplitude = 0.5 * 32767.0;  // strongest aircraft, int16 counts

constexpr uint32_t kMinSampleRateHz = 2'000'000;  // one 0.5 us PPM chip must span >= 1 sample
constexpr double kMinRenderRateHz = 16e6;          // see Config::oversample

}  // namespace

// --- Public encoders -----------------------------------------------------

uint32_t crc24(const uint8_t* data, size_t nbytes) {
    uint32_t crc = 0;
    for (size_t i = 0; i < nbytes; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            const uint32_t feedback = ((crc >> 23) & 1) ^ ((data[i] >> bit) & 1);
            crc = (crc << 1) & 0xFFFFFF;
            if (feedback) crc ^= 0xFFF409;
        }
    }
    return crc;
}

Frame encode_acquisition_squitter(uint32_t icao) {
    Frame f;
    f.num_bits = 56;
    put_bits(f.bytes, 0, 5, 11);
    put_bits(f.bytes, 5, 3, kCapability);
    put_bits(f.bytes, 8, 24, icao & 0xFFFFFF);
    put_bits(f.bytes, 32, 24, crc24(f.bytes.data(), 4));
    return f;
}

Frame encode_identification(uint32_t icao, std::string_view callsign) {
    uint64_t chars = 0;
    for (size_t i = 0; i < 8; i++) {
        const char c = i < callsign.size() ? callsign[i] : ' ';
        uint64_t v = 32;
        if (c >= 'A' && c <= 'Z') v = static_cast<uint64_t>(c - 'A' + 1);
        else if (c >= '0' && c <= '9') v = static_cast<uint64_t>(48 + (c - '0'));
        chars = (chars << 6) | v;
    }
    constexpr uint64_t kTypeCode = 4, kCategory = 0;
    return make_df17(icao, (kTypeCode << 51) | (kCategory << 48) | chars);
}

Frame encode_airborne_position(uint32_t icao, int alt_ft, double lat_deg, double lon_deg, bool odd) {
    const int n = std::clamp((alt_ft + 1000 + 12) / 25, 0, 0x7FF);  // 25 ft steps from -1000 ft
    const uint64_t alt12 = (static_cast<uint64_t>(n >> 4) << 5) | (1u << 4) | static_cast<uint64_t>(n & 0xF);

    const int i = odd ? 1 : 0;
    constexpr double kCprScale = 131072.0;  // 2^17
    const double dlat = 360.0 / (60 - i);
    const double yz = std::floor(kCprScale * pmod(lat_deg, dlat) / dlat + 0.5);
    const double rlat = dlat * (yz / kCprScale + std::floor(lat_deg / dlat));
    const double dlon = 360.0 / std::max(cpr_nl(rlat) - i, 1);
    const double xz = std::floor(kCprScale * pmod(lon_deg, dlon) / dlon + 0.5);
    const uint64_t lat_cpr = static_cast<uint64_t>(yz) & 0x1FFFF;
    const uint64_t lon_cpr = static_cast<uint64_t>(xz) & 0x1FFFF;

    constexpr uint64_t kTypeCode = 11;
    return make_df17(icao, (kTypeCode << 51) | (alt12 << 36) | (static_cast<uint64_t>(i) << 34) | (lat_cpr << 17) |
                               lon_cpr);
}

Frame encode_velocity(uint32_t icao, double east_kt, double north_kt, int vrate_fpm) {
    auto speed_field = [](double kt) { return std::min<uint64_t>(static_cast<uint64_t>(std::llround(std::abs(kt))) + 1, 1023); };
    const uint64_t vr = std::min<uint64_t>(static_cast<uint64_t>(std::llround(std::abs(vrate_fpm) / 64.0)) + 1, 511);
    constexpr uint64_t kTypeCode = 19, kSubtype = 1;
    const uint64_t me = (kTypeCode << 51) | (kSubtype << 48) | (static_cast<uint64_t>(east_kt < 0) << 42) |
                        (speed_field(east_kt) << 32) | (static_cast<uint64_t>(north_kt < 0) << 31) |
                        (speed_field(north_kt) << 21) | (static_cast<uint64_t>(vrate_fpm < 0) << 19) | (vr << 10);
    return make_df17(icao, me);
}

namespace {

[[noreturn]] void scenario_error(size_t index, const std::string& what) {
    throw std::invalid_argument("adsb_sim scenario: aircraft[" + std::to_string(index) + "]: " + what);
}

template <typename T>
T required(const nlohmann::json& j, size_t index, const char* key) {
    if (!j.contains(key)) scenario_error(index, std::string("missing \"") + key + "\"");
    try {
        return j.at(key).get<T>();
    } catch (const nlohmann::json::exception&) {
        scenario_error(index, std::string("\"") + key + "\" has the wrong type");
    }
}

template <typename T>
T optional(const nlohmann::json& j, size_t index, const char* key, T fallback) {
    return j.contains(key) ? required<T>(j, index, key) : fallback;
}

void check_range(size_t index, const char* key, double v, double lo, double hi, bool lo_exclusive = false) {
    if (!(v >= lo && v <= hi) || (lo_exclusive && v == lo))
        scenario_error(index, std::string("\"") + key + "\" out of range");
}

AircraftSpec parse_aircraft(const nlohmann::json& j, size_t index) {
    if (!j.is_object()) scenario_error(index, "must be an object");
    static const std::set<std::string> kKeys = {"icao",      "callsign",  "lat_deg",   "lon_deg", "alt_ft",
                                                "ground_speed_kt", "track_deg", "vrate_fpm", "amplitude", "period_jitter"};
    for (const auto& [key, value] : j.items())
        if (!kKeys.count(key)) scenario_error(index, "unknown key \"" + key + "\"");

    AircraftSpec a;
    const auto icao = required<std::string>(j, index, "icao");
    if (icao.size() != 6 || icao.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        scenario_error(index, "\"icao\" must be 6 hex digits");
    a.icao = static_cast<uint32_t>(std::stoul(icao, nullptr, 16));

    a.callsign = optional<std::string>(j, index, "callsign", "");
    if (a.callsign.size() > 8 || a.callsign.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ") != std::string::npos)
        scenario_error(index, "\"callsign\" must be at most 8 characters of A-Z, 0-9 and space");

    a.lat_deg = required<double>(j, index, "lat_deg");
    check_range(index, "lat_deg", a.lat_deg, -90, 90);
    a.lon_deg = required<double>(j, index, "lon_deg");
    check_range(index, "lon_deg", a.lon_deg, -180, 180);
    a.alt_ft = required<int>(j, index, "alt_ft");
    check_range(index, "alt_ft", a.alt_ft, -1000, kMaxAltFt);
    a.ground_speed_kt = required<double>(j, index, "ground_speed_kt");
    check_range(index, "ground_speed_kt", a.ground_speed_kt, 0, 1e5);
    a.track_deg = required<double>(j, index, "track_deg");
    check_range(index, "track_deg", a.track_deg, -1e6, 1e6);
    a.vrate_fpm = optional<int>(j, index, "vrate_fpm", 0);
    a.amplitude = optional<double>(j, index, "amplitude", 1.0);
    check_range(index, "amplitude", a.amplitude, 0, 1, /*lo_exclusive=*/true);
    a.period_jitter = optional<double>(j, index, "period_jitter", a.period_jitter);
    check_range(index, "period_jitter", a.period_jitter, 0, 0.5);
    return a;
}

}  // namespace

std::vector<AircraftSpec> parse_scenario(std::string_view json_text) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::exception& err) {
        throw std::invalid_argument(std::string("adsb_sim scenario: invalid JSON: ") + err.what());
    }
    if (!doc.is_object() || !doc.contains("aircraft") || !doc["aircraft"].is_array())
        throw std::invalid_argument("adsb_sim scenario: expected an object with an \"aircraft\" array");
    for (const auto& [key, value] : doc.items())
        if (key != "aircraft") throw std::invalid_argument("adsb_sim scenario: unknown top-level key \"" + key + "\"");

    std::vector<AircraftSpec> out;
    std::set<uint32_t> seen;
    for (size_t i = 0; i < doc["aircraft"].size(); i++) {
        out.push_back(parse_aircraft(doc["aircraft"][i], i));
        if (!seen.insert(out.back().icao).second) scenario_error(i, "duplicate \"icao\"");
    }
    if (out.empty()) throw std::invalid_argument("adsb_sim scenario: \"aircraft\" is empty");
    return out;
}

std::vector<AircraftSpec> load_scenario(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::invalid_argument("adsb_sim scenario: cannot open " + path);
    std::ostringstream text;
    text << in.rdbuf();
    try {
        return parse_scenario(text.str());
    } catch (const std::invalid_argument& err) {
        throw std::invalid_argument(path + ": " + err.what());
    }
}

// --- Simulator -----------------------------------------------------------

namespace {

double bessel_i0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 60; k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-17 * sum) break;
    }
    return sum;
}

constexpr double kKaiserBeta = 5.0;      // ~50 dB stopband
constexpr unsigned kTapsPerOversample = 16;  // half-length = this * oversample; transition band ~0.1 * output rate

// Symmetric lowpass, 2*half+1 taps at the render rate, unity DC gain. `cutoff` is in cycles per render sample.
xt::xtensor<double, 1> design_lowpass(unsigned half, double cutoff) {
    auto taps = xt::xtensor<double, 1>::from_shape({2 * half + 1});  // every tap is assigned below
    const double i0_beta = bessel_i0(kKaiserBeta);
    double sum = 0;
    for (int m = -static_cast<int>(half); m <= static_cast<int>(half); m++) {
        const double x = 2.0 * cutoff * m;
        const double sinc = m == 0 ? 1.0 : std::sin(kPi * x) / (kPi * x);
        const double r = static_cast<double>(m) / half;
        const double window = bessel_i0(kKaiserBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0_beta;
        taps[m + half] = 2.0 * cutoff * sinc * window;
        sum += taps[m + half];
    }
    taps /= sum;
    return taps;
}

}  // namespace

struct Simulator::Impl {
    // One merged run of chips: the 50% crossings of its rising and falling edges, in render-rate sample units.
    struct Pulse {
        double rise, fall;
    };

    struct Emitter {
        size_t aircraft;
        Kind kind;
        uint64_t period_samples;
        uint64_t next_start;  // output samples
        bool odd = false;     // Position: which CPR frame goes next
        Rng rng;
    };

    struct Tx {
        std::vector<Pulse> pulses;  // ordered in time
        double support_end;         // render-rate sample index past which this transmission is exactly zero
        uint64_t start;             // nominal start, output samples (ordering key)
        size_t emitter;
        float amp_i, amp_q;
    };

    Config cfg;
    const std::vector<AircraftSpec>& aircraft = cfg.aircraft;
    unsigned oversample;   // render rate / output rate
    unsigned half_taps;    // 0 when oversample == 1 (no filtering)
    double render_rate;
    double rise_h, fall_h, jitter_h, lead_h;  // pulse-shape parameters in render-rate samples
    xt::xtensor<float, 1> taps;
    xt::xtensor<double, 1> phase_cos, phase_sin;  // per aircraft
    std::vector<Emitter> emitters;
    std::vector<Tx> active;  // ordered by (start, emitter)
    std::vector<Transmission> recorded;  // see Config::record_transmissions
    // Per-call scratch, allocated once (uninitialized) at the largest size a call can need, so a call's
    // window size never triggers a reallocation. Each call initializes only the part it uses. Single
    // precision: the signal peaks at ~16k counts, so float's ~1e-3 count resolution is far below the int16
    // quantization step, and it halves the memory traffic and doubles the SIMD width.
    xt::xtensor<float, 1> xi, xq;   // render-rate window
    xt::xtensor<uint8_t, 1> dirty;  // per output sample: window contains signal
    xt::xtensor<float, 1> si, sq;   // FIR output per output sample (only used when half_taps > 0)
    uint64_t noise_key;          // see sim_kernels.ispc: noise is a pure function of (key, sample index)
    double noise_sigma;
    uint64_t next_sample = 0;

    void generate_block(std::span<int16_t> iq);  // iq.size() <= 2 * kSubBlockSamples

    explicit Impl(const Config& c) : cfg(c) {
        if (cfg.sample_rate_hz < kMinSampleRateHz)
            throw std::invalid_argument("adsb_sim: sample rate must be >= " + std::to_string(kMinSampleRateHz) + " Hz");
        if (cfg.aircraft.empty()) throw std::invalid_argument("adsb_sim: at least one aircraft is required");
        if (!(cfg.pulse_rise_s >= 0 && cfg.pulse_rise_s <= 0.25e-6) || !(cfg.pulse_fall_s >= 0 && cfg.pulse_fall_s <= 0.25e-6))
            throw std::invalid_argument("adsb_sim: pulse rise/fall time must be in [0, 250 ns]");
        if (!(cfg.pulse_jitter_s >= 0 && cfg.pulse_jitter_s <= 0.1e-6))
            throw std::invalid_argument("adsb_sim: pulse jitter must be in [0, 100 ns]");
        if (!(cfg.rx_cutoff > 0 && cfg.rx_cutoff <= 0.5))
            throw std::invalid_argument("adsb_sim: rx cutoff must be in (0, 0.5] (fraction of the sample rate)");
        if (cfg.oversample > 64) throw std::invalid_argument("adsb_sim: oversample must be <= 64");

        oversample = cfg.oversample ? cfg.oversample
                                    : std::max(1u, static_cast<unsigned>(std::ceil(kMinRenderRateHz / cfg.sample_rate_hz)));
        render_rate = static_cast<double>(oversample) * cfg.sample_rate_hz;
        half_taps = oversample > 1 ? kTapsPerOversample * oversample : 0;
        if (half_taps) taps = xt::cast<float>(design_lowpass(half_taps, cfg.rx_cutoff / oversample));  // designed in double
        const size_t max_window = (kSubBlockSamples - 1) * oversample + 2 * half_taps + 1;
        xi = xt::xtensor<float, 1>::from_shape({max_window});
        xq = xt::xtensor<float, 1>::from_shape({max_window});
        dirty = xt::xtensor<uint8_t, 1>::from_shape({kSubBlockSamples});
        if (half_taps) {
            si = xt::xtensor<float, 1>::from_shape({kSubBlockSamples});
            sq = xt::xtensor<float, 1>::from_shape({kSubBlockSamples});
        }
        rise_h = cfg.pulse_rise_s * render_rate;
        fall_h = cfg.pulse_fall_s * render_rate;
        jitter_h = cfg.pulse_jitter_s * render_rate;
        lead_h = 0.5 * rise_h + jitter_h + 2.0;  // how far a transmission's support can precede its nominal start

        uint64_t noise_sm = cfg.seed ^ 0x6E6F697365ull;  // "noise"
        noise_key = splitmix64(noise_sm);
        noise_sigma = kBaseAmplitude / std::sqrt(2.0 * std::pow(10.0, cfg.snr_db / 10.0));

        phase_cos = xt::xtensor<double, 1>::from_shape({aircraft.size()});
        phase_sin = xt::xtensor<double, 1>::from_shape({aircraft.size()});
        Rng phase_rng(cfg.seed, 1);
        for (size_t a = 0; a < aircraft.size(); a++) {
            const double phase = 2.0 * kPi * phase_rng.uniform();
            phase_cos[a] = std::cos(phase);
            phase_sin[a] = std::sin(phase);
            for (const auto& sched : kSchedules) {
                Rng rng(cfg.seed, 100 + emitters.size());
                const auto period = static_cast<uint64_t>(std::llround(sched.period_s * cfg.sample_rate_hz));
                const auto first = static_cast<uint64_t>(std::llround(rng.uniform() * sched.max_first_offset_s * cfg.sample_rate_hz));
                emitters.push_back({a, sched.kind, period, first, false, rng});
            }
        }
    }

    Frame frame_for(const Emitter& e) const {
        const AircraftSpec& a = aircraft[e.aircraft];
        const Kinematics k = kinematics_at(a, static_cast<double>(e.next_start) / cfg.sample_rate_hz);
        switch (e.kind) {
            case Kind::Squitter: return encode_acquisition_squitter(a.icao);
            case Kind::Identification: return encode_identification(a.icao, a.callsign);
            case Kind::Position: return encode_airborne_position(a.icao, k.alt_ft, k.lat_deg, k.lon_deg, e.odd);
            case Kind::Velocity: return encode_velocity(a.icao, k.east_kt, k.north_kt, a.vrate_fpm);
        }
        __builtin_unreachable();
    }

    Tx make_tx(size_t emitter_idx) {
        Emitter& e = emitters[emitter_idx];
        const Frame f = frame_for(e);

        Tx tx;
        tx.emitter = emitter_idx;
        tx.start = e.next_start;

        std::array<bool, kMaxChips> chips{};
        const unsigned num_chips = kPreambleChips + 2 * f.num_bits;
        // Preamble pulses at 0, 1, 3.5 and 4.5 us.
        for (unsigned c : {0u, 2u, 7u, 9u}) chips[c] = true;
        // PPM: a 1 bit is a pulse in the first half-symbol, a 0 in the second.
        for (unsigned i = 0; i < f.num_bits; i++) {
            const bool bit = (f.bytes[i / 8] >> (7 - i % 8)) & 1;
            chips[kPreambleChips + 2 * i + (bit ? 0 : 1)] = true;
        }

        // Sub-sample start time, then one merged pulse per run of adjacent chips (a 0 bit followed by a 1
        // bit is a single 1 us pulse on the air), each edge displaced by its own timing error.
        const double t0 = (static_cast<double>(e.next_start) + e.rng.uniform()) * oversample;
        if (cfg.record_transmissions) recorded.push_back({t0 / oversample, e.aircraft, f});
        const double chip_h = render_rate / static_cast<double>(kChipsPerSecond);
        auto edge_error = [&] { return jitter_h * (2.0 * e.rng.uniform() - 1.0); };
        for (unsigned c = 0; c < num_chips; c++) {
            if (!chips[c]) continue;
            unsigned end = c;
            while (end + 1 < num_chips && chips[end + 1]) end++;
            const double rise = t0 + c * chip_h + edge_error();
            const double fall = t0 + (end + 1) * chip_h + edge_error();
            tx.pulses.push_back({rise, fall});
            c = end;
        }
        tx.support_end = tx.pulses.back().fall + 0.5 * fall_h + jitter_h + 2.0;

        const double amp = kBaseAmplitude * aircraft[e.aircraft].amplitude;
        tx.amp_i = static_cast<float>(amp * phase_cos[e.aircraft]);
        tx.amp_q = static_cast<float>(amp * phase_sin[e.aircraft]);

        if (e.kind == Kind::Position) e.odd = !e.odd;
        const double jitter = 1.0 + aircraft[e.aircraft].period_jitter * (2.0 * e.rng.uniform() - 1.0);
        e.next_start += static_cast<uint64_t>(std::llround(static_cast<double>(e.period_samples) * jitter));
        return tx;
    }

    // Flags the first `n` output samples' entries in `dirty` whose filter window covers render-window offsets
    // [r_from, r_to].
    void mark_dirty(int64_t r_from, int64_t r_to, size_t n) {
        const int64_t N = oversample, span = 2 * static_cast<int64_t>(half_taps);
        auto floor_div = [](int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
        const int64_t k_lo = std::max<int64_t>(0, floor_div(r_from - span + N - 1, N));
        const int64_t k_hi = std::min<int64_t>(static_cast<int64_t>(n) - 1, floor_div(r_to, N));
        for (int64_t k = k_lo; k <= k_hi; k++) dirty[k] = 1;
    }

    // Adds the transmission's envelope at render-rate samples [lo, hi] to xi/xq; `n` is the number of output
    // samples of the call.
    void add_tx(const Tx& tx, int64_t lo, int64_t hi, size_t n) {
        const auto first = std::partition_point(tx.pulses.begin(), tx.pulses.end(),
                                                [&](const Pulse& p) { return p.fall + 0.5 * fall_h + jitter_h < static_cast<double>(lo); });
        const double tiny = 1e-9;  // zero-length edges are a step
        const double rise_len = std::max(rise_h, tiny), fall_len = std::max(fall_h, tiny);
        for (auto it = first; it != tx.pulses.end(); ++it) {
            const double bottom_rise = it->rise - 0.5 * rise_h;  // envelope reaches 0 here and 1 at bottom_rise + rise_len
            const double bottom_fall = it->fall + 0.5 * fall_h;  // and is back to 0 here
            if (bottom_rise > static_cast<double>(hi)) break;
            const auto from = std::max(lo, static_cast<int64_t>(std::ceil(bottom_rise)));
            const auto to = std::min(hi, static_cast<int64_t>(std::floor(bottom_fall)));
            if (from > to) continue;
            mark_dirty(from - lo, to - lo, n);
            // An explicit loop: the equivalent xtensor expression (arange/minimum/clip added into a view)
            // measured ~7% slower at 20 Msps, where this is the hot path.
            for (int64_t h = from; h <= to; h++) {
                const double t = static_cast<double>(h);
                const double v = std::clamp(std::min((t - bottom_rise) / rise_len, (bottom_fall - t) / fall_len), 0.0, 1.0);
                xi[h - lo] += tx.amp_i * v;
                xq[h - lo] += tx.amp_q * v;
            }
        }
    }
};

Simulator::Simulator(const Config& cfg) : m_impl(std::make_unique<Impl>(cfg)) {}
Simulator::~Simulator() = default;

std::vector<Transmission> Simulator::take_transmissions() { return std::exchange(m_impl->recorded, {}); }

void Simulator::generate(std::span<int16_t> iq) {
    // Work in cache-sized pieces: the render buffers are two doubles per sample, so an unbounded call
    // would stream tens of MB through memory several times (measured: ~335 Msps at 2M samples per call
    // vs ~480 Msps in pieces). The output is chunk-independent, so this changes nothing but speed. It is
    // single-threaded: a piece is only ~70 us of work, so fork-join threading through the ispc task
    // system (at the time, a thread per task per launch) measured 4-6x slower, not faster.
    assert(iq.size() % 2 == 0);
    const size_t n = iq.size() / 2;
    for (size_t done = 0; done < n;) {
        const size_t len = std::min(n - done, kSubBlockSamples);
        m_impl->generate_block(iq.subspan(2 * done, 2 * len));
        done += len;
    }
}

void Simulator::Impl::generate_block(std::span<int16_t> iq) {
    Impl& d = *this;
    const size_t n = iq.size() / 2;
    const uint64_t s0 = d.next_sample;
    const uint64_t s1 = s0 + n;
    const int64_t N = d.oversample, M = d.half_taps;

    // Output sample k needs render samples [k*N - M, k*N + M]; a transmission is a pure function of its
    // parameters, so the window is simply re-rendered each call and nothing but the transmission list
    // carries over -- which keeps the output independent of how the caller chunks the stream.
    const int64_t lo = static_cast<int64_t>(s0) * N - M;
    const int64_t hi = static_cast<int64_t>(s1 - 1) * N + M;

    // Every transmission is created in the first call whose window can reach it, so ordering new ones by
    // (start, emitter) keeps `active` -- and thus the float summation order at overlaps -- independent of
    // the chunking.
    std::vector<Tx> fresh;
    for (size_t i = 0; i < d.emitters.size(); i++) {
        while (static_cast<double>(d.emitters[i].next_start) * d.oversample - d.lead_h <= static_cast<double>(hi))
            fresh.push_back(d.make_tx(i));
    }
    std::sort(fresh.begin(), fresh.end(), [&](const auto& a, const auto& b) {
        return a.start != b.start ? a.start < b.start : a.emitter < b.emitter;
    });
    d.active.insert(d.active.end(), std::make_move_iterator(fresh.begin()), std::make_move_iterator(fresh.end()));

    const auto window = static_cast<size_t>(hi - lo + 1);
    assert(window <= d.xi.size() && n <= d.dirty.size());
    std::fill_n(d.xi.data(), window, 0.0f);
    std::fill_n(d.xq.data(), window, 0.0f);
    std::fill_n(d.dirty.data(), n, uint8_t{0});
    for (const auto& tx : d.active) d.add_tx(tx, lo, hi, n);

    // Signal per output sample: the render buffer itself at render rate == output rate, else its
    // anti-alias-filtered, decimated version (skipped, i.e. left zero, where the window has no pulse).
    const float* sig_i = d.xi.data();
    const float* sig_q = d.xq.data();
    if (M != 0) {
        float* out_i = d.si.data();
        float* out_q = d.sq.data();
        const float* tap = d.taps.data();
        const size_t num_taps = d.taps.size();
        const uint8_t* dirty = d.dirty.data();
        for (size_t k = 0; k < n; k++) {
            if (!dirty[k]) {
                out_i[k] = out_q[k] = 0.0f;
                continue;
            }
            float acc_i = 0.0f, acc_q = 0.0f;
            const float* pi = d.xi.data() + k * N;
            const float* pq = d.xq.data() + k * N;
            for (size_t j = 0; j < num_taps; j++) {
                acc_i += tap[j] * pi[j];
                acc_q += tap[j] * pq[j];
            }
            out_i[k] = acc_i;
            out_q[k] = acc_q;
        }
        sig_i = d.si.data();
        sig_q = d.sq.data();
    }
    static_assert(std::endian::native == std::endian::little, "sim_finish packs (I, Q) int16 pairs little-endian");
    ispc::sim_finish(sig_i, sig_q, s0, d.noise_key, static_cast<float>(d.noise_sigma), static_cast<int>(n),
                     reinterpret_cast<int32_t*>(iq.data()));

    // Keep a transmission until no later output sample's window can reach it.
    const double next_lo = static_cast<double>(static_cast<int64_t>(s1) * N - M);
    std::erase_if(d.active, [next_lo](const auto& tx) { return tx.support_end < next_lo; });
    d.next_sample = s1;
}

}  // namespace adsb_sim
