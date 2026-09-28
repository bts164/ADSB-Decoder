#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file
 * Simulated ADS-B (Mode S 1090ES) baseband IQ, used by `vita49_send sim` and adsb_sim_eval.
 *
 * Encodes DF17 extended squitters (identification, airborne position, velocity) and DF11 acquisition
 * squitters for a set of aircraft (from a JSON scenario) moving at constant velocity, and renders them as
 * PPM pulses at baseband (centered on 1090 MHz) plus AWGN at an arbitrary sample rate.
 *
 * Pulses are on-off keyed, as on a real transponder: 0.5 us chips with trapezoidal edges (finite rise and
 * fall time), a small random position error on every pulse edge, and a random sub-sample start time per
 * message. The receiver's anti-alias filter is modelled by rendering at an integer multiple of the output
 * rate (when that rate is too low to resolve the edges), lowpass filtering with a Kaiser-windowed sinc FIR
 * and decimating. Noise is white and added at the output rate.
 *
 * The output is a pure function of (Config, sample index): the same seed yields the same samples no
 * matter how the stream is chunked across Simulator::generate() calls.
 */
namespace adsb_sim {

/** One encoded Mode S frame. */
struct Frame {
    std::array<uint8_t, 14> bytes{};  /**< big-endian bit order, first bit = MSB of bytes[0] */
    unsigned num_bits = 0;            /**< 56 (short) or 112 (long) */
};

/**
 * Mode S CRC-24 remainder of `nbytes` bytes (data * x^24 mod generator 0xFFF409). Over a complete frame
 * including its parity field, a valid frame gives 0.
 */
uint32_t crc24(const uint8_t* data, size_t nbytes);

/**
 * @name Frame encoders
 * @{
 */

/** DF11 all-call reply/squitter (II=0, so PI is the plain CRC). */
Frame encode_acquisition_squitter(uint32_t icao);
/**
 * DF17 TC=4 identification. `callsign` is truncated/space-padded to 8 chars; characters outside
 * [A-Z0-9 ] become spaces.
 */
Frame encode_identification(uint32_t icao, std::string_view callsign);
/**
 * DF17 TC=11 airborne position with barometric altitude. Altitude is quantized to 25 ft (Q=1 encoding);
 * lat/lon are 17-bit CPR encoded for the even (`odd` = false) or odd frame.
 */
Frame encode_airborne_position(uint32_t icao, int alt_ft, double lat_deg, double lon_deg, bool odd);
/** DF17 TC=19 subtype 1 (ground speed). Speeds are quantized to whole knots and vertical rate to 64 fpm. */
Frame encode_velocity(uint32_t icao, double east_kt, double north_kt, int vrate_fpm);

/** @} */

/** One simulated aircraft. */
struct AircraftSpec {
    uint32_t icao;
    std::string callsign;     /**< up to 8 chars of [A-Z0-9 ] */
    double lat_deg, lon_deg;  /**< position at t=0 */
    int alt_ft;               /**< altitude at t=0 */
    double ground_speed_kt;
    double track_deg;         /**< clockwise from north */
    int vrate_fpm;
    double amplitude = 1.0;      /**< received amplitude relative to a full-amplitude aircraft, (0, 1] */
    double period_jitter = 0.1;  /**< each inter-message interval is nominal * (1 +/- period_jitter), uniform */
};

/**
 * @name Scenario loading
 * A scenario is a JSON object `{"aircraft": [ {...}, ... ]}`. Per aircraft:
 *
 * | key               | value                          | required / default |
 * |-------------------|--------------------------------|--------------------|
 * | `icao`            | string, 6 hex digits           | required, unique   |
 * | `lat_deg`         | number in [-90, 90]            | required           |
 * | `lon_deg`         | number in [-180, 180]          | required           |
 * | `alt_ft`          | integer in [-1000, 50175]      | required           |
 * | `ground_speed_kt` | number >= 0                    | required           |
 * | `track_deg`       | number, clockwise from north   | required           |
 * | `callsign`        | string, <= 8 chars of [A-Z0-9 ]| blank              |
 * | `vrate_fpm`       | integer                        | 0                  |
 * | `amplitude`       | number in (0, 1]               | 1                  |
 * | `period_jitter`   | number in [0, 0.5]             | 0.1                |
 *
 * Unknown keys are rejected so typos don't silently fall back to defaults.
 * @{
 */

/**
 * Parses a scenario from JSON text.
 * @throws std::invalid_argument with a message naming the offending field
 */
std::vector<AircraftSpec> parse_scenario(std::string_view json_text);
/**
 * Reads and parses a scenario file.
 * @throws std::invalid_argument with a message naming the offending field
 */
std::vector<AircraftSpec> load_scenario(const std::string& path);

/** @} */

/** Simulator settings. */
struct Config {
    uint32_t sample_rate_hz = 2'400'000;  /**< must be >= 2 MHz (one 0.5 us PPM chip per sample) */
    uint64_t seed = 1;
    double snr_db = 30.0;  /**< pulse power of a unit-amplitude aircraft over noise power, per complex sample */
    std::vector<AircraftSpec> aircraft;  /**< at least one; see load_scenario() */

    /**
     * Transmit pulse rise time: 0%-100% ramp time of a linear edge centred on the nominal chip boundary
     * (real transponders: roughly 50-100 ns).
     */
    double pulse_rise_s = 75e-9;
    /** Transmit pulse fall time (real transponders: roughly 50-200 ns). */
    double pulse_fall_s = 100e-9;
    /** Every pulse edge is displaced by an independent uniform error in +/- this (spec tolerance: 50 ns). */
    double pulse_jitter_s = 25e-9;

    /**
     * Receiver anti-alias filter: -6 dB cutoff as a fraction of the output sample rate (<= 0.5). Only
     * applied when the render rate exceeds the output rate (see `oversample`): at high output rates the
     * trapezoids are already band-limited far below Nyquist.
     */
    double rx_cutoff = 0.45;
    /**
     * Render rate = oversample * sample_rate_hz. 0 = automatic: the smallest integer that reaches 16 MHz
     * (1 at output rates >= that).
     */
    unsigned oversample = 0;

    /**
     * Keep a log of every transmission (see Simulator::take_transmissions), as ground truth for scoring a
     * decoder. Off by default: a long-running stream would otherwise grow it without bound.
     */
    bool record_transmissions = false;
};

/** One simulated transmission, as recorded when Config::record_transmissions is set. */
struct Transmission {
    double start;     /**< output-sample index (fractional) of the first preamble pulse's nominal leading edge */
    size_t aircraft;  /**< index into Config::aircraft */
    Frame frame;
};

/** Generates the simulated IQ stream. */
class Simulator {
public:
    /** @throws std::invalid_argument on a bad Config */
    explicit Simulator(const Config& cfg);
    ~Simulator();
    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;

    /**
     * Fills `iq` with the next iq.size()/2 complex samples as interleaved int16 I,Q (host byte order).
     * The size must be even.
     */
    void generate(std::span<int16_t> iq);

    /**
     * Transmissions recorded since the last call (Config::record_transmissions), in no particular order.
     * A transmission is recorded as soon as generate() first renders any part of it, so the last few may
     * extend past the samples generated so far.
     */
    std::vector<Transmission> take_transmissions();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace adsb_sim
