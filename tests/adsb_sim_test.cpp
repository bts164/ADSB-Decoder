// Tests for adsb_sim: message encoding (cross-checked against the project's
// own decoder in aircraft.cpp), PPM modulation (via a small reference slicer),
// and determinism.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "sim/adsb_sim.h"
#include "decode/aircraft.h"

namespace {

int g_failures = 0;

template <typename F>
bool throws_invalid(F&& f) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

std::vector<uint8_t> from_hex(const std::string& hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return out;
}

// Right-justified 112-bit payload, the form aircraft.cpp's decoder takes.
unsigned __int128 payload_of(const adsb_sim::Frame& f) {
    unsigned __int128 p = 0;
    for (unsigned i = 0; i < f.num_bits / 8; i++) p = (p << 8) | f.bytes[i];
    return p;
}

void test_crc() {
    // Well-known DF17 identification frame from "The 1090MHz Riddle".
    const auto msg = from_hex("8D4840D6202CC371C32CE0576098");
    CHECK(adsb_sim::crc24(msg.data(), 11) == 0x576098);
    CHECK(adsb_sim::crc24(msg.data(), 14) == 0);
}

void test_encoders_crc_and_decode() {
    using namespace adsb_sim;
    const uint32_t icao = 0x4840D6;

    for (const Frame& f : {encode_identification(icao, "KLM1023"), encode_airborne_position(icao, 38000, 52.2572, 3.9194, false),
                           encode_airborne_position(icao, 38000, 52.2572, 3.9194, true), encode_velocity(icao, 120, -80, -640)}) {
        CHECK(f.num_bits == 112);
        CHECK(crc24(f.bytes.data(), 14) == 0);
        CHECK(((f.bytes[0] >> 3) & 0x1F) == 17);
    }
    const Frame sq = encode_acquisition_squitter(icao);
    CHECK(sq.num_bits == 56);
    CHECK(crc24(sq.bytes.data(), 7) == 0);
    CHECK(((sq.bytes[0] >> 3) & 0x1F) == 11);

    // Round-trip through the real decoder. Several hemispheres so the CPR
    // negative-lat/lon wrapping is exercised.
    struct Pos { double lat, lon; };
    for (Pos p : {Pos{52.2572, 3.9194}, Pos{42.3601, -71.0589}, Pos{-33.9, 151.2}, Pos{-23.5, -46.6}}) {
        AircraftState ac;
        ac.icao = icao;
        update_aircraft_from_me(ac, payload_of(encode_identification(icao, "KLM1023")), 0.0);
        update_aircraft_from_me(ac, payload_of(encode_airborne_position(icao, 38000, p.lat, p.lon, false)), 1.0);
        update_aircraft_from_me(ac, payload_of(encode_airborne_position(icao, 38000, p.lat, p.lon, true)), 2.0);
        update_aircraft_from_me(ac, payload_of(encode_velocity(icao, 120, -80, -640)), 3.0);

        CHECK(ac.callsign && *ac.callsign == "KLM1023");
        CHECK(ac.altitude_ft && *ac.altitude_ft == 38000);
        CHECK(ac.lat && std::abs(*ac.lat - p.lat) < 1e-3);
        CHECK(ac.lon && std::abs(*ac.lon - p.lon) < 1e-3);
        CHECK(ac.ground_speed_kt && std::abs(*ac.ground_speed_kt - std::sqrt(120.0 * 120 + 80.0 * 80)) < 1e-6);
        CHECK(ac.track_deg && std::abs(*ac.track_deg - std::atan2(120.0, -80.0) * 180.0 / M_PI) < 1e-6);
        CHECK(ac.vertical_rate_fpm && *ac.vertical_rate_fpm == -640);
    }
}

// Frame bits from hex, first bit in bit 111 as AdsbFrame has them: a 56-bit
// frame lands in the leading 56.
unsigned __int128 frame_payload(const std::string& hex) {
    unsigned __int128 p = 0;
    for (uint8_t b : from_hex(hex)) p = (p << 8) | b;
    return hex.size() == 14 ? p << 56 : p;
}

// Squawk and category decodes, against frames decoded by pyModeS 2.21.
void test_squawk_and_category() {
    AircraftState ac;
    CHECK(update_aircraft_from_frame(ac, 5, frame_payload("2A00516D492B80"), 0.0));  // DF5
    CHECK(ac.squawk == "0356");
    CHECK(!update_aircraft_from_frame(ac, 5, frame_payload("2A00516D492B80"), 0.0));

    // TC28 subtype 1 (emergency status).
    CHECK(update_aircraft_from_me(ac, frame_payload("8D4840D6E12AAA000000003CF5CE"), 0.0));
    CHECK(ac.squawk == "7700");
    CHECK(update_aircraft_from_me(ac, frame_payload("8D4840D6E1167500000000D13CE2"), 0.0));
    CHECK(ac.squawk == "2137");

    // Identification: TC4 category 3 (A3, large), TC2 category 1 (C1, emergency vehicle).
    update_aircraft_from_me(ac, frame_payload("8D4840D6232CC371C32CE0CC1B88"), 0.0);
    CHECK(ac.category == "A3");
    CHECK(ac.callsign == "KLM1023");
    update_aircraft_from_me(ac, frame_payload("8D4840D6112CC371C32CE0C32F0A"), 0.0);
    CHECK(ac.category == "C1");
    // Category 0 (no information) leaves it as it was.
    update_aircraft_from_me(ac, frame_payload("8D4840D6202CC371C32CE0576098"), 0.0);
    CHECK(ac.category == "C1");

    update_aircraft_reception(ac, 1120.0f, 112);
    CHECK(ac.messages == 1 && ac.signal_db && std::abs(*ac.signal_db - 20.0) < 1e-6);
    update_aircraft_reception(ac, 112.0f, 112);
    CHECK(ac.messages == 2 && std::abs(*ac.signal_db - 18.0) < 1e-6);
}

// Replies other than DF17/18, and the ES fields beyond the basic set. Frames from a real recording (and
// pyModeS's own test frames for TC19 subtype 3 and TC29 with autopilot modes), checked against pyModeS 2.21.
void test_reply_and_state_decodes() {
    AircraftState ac;
    CHECK(update_aircraft_from_frame(ac, 4, frame_payload("20000516109922"), 0.0));  // DF4, FS 0
    CHECK(ac.altitude_ft == 7150 && ac.on_ground == false && !ac.alert && !ac.spi);
    update_aircraft_from_frame(ac, 4, frame_payload("20001b90a31844"), 0.0);
    CHECK(ac.altitude_ft == 43000);
    update_aircraft_from_frame(ac, 0, frame_payload("02e1913d34ba4e"), 0.0);  // DF0, VS 0
    CHECK(ac.altitude_ft == 26925 && ac.on_ground == false);

    // Flight status, by rewriting FS in the DF4 frame above.
    update_aircraft_from_frame(ac, 4, frame_payload("22000516109922"), 0.0);  // 2: airborne, alert
    CHECK(ac.alert && !ac.spi && ac.on_ground == false);
    update_aircraft_from_frame(ac, 4, frame_payload("21000516109922"), 0.0);  // 1: on ground
    CHECK(!ac.alert && ac.on_ground == true);
    update_aircraft_from_frame(ac, 4, frame_payload("25000516109922"), 0.0);  // 5: SPI, air or ground
    CHECK(ac.spi && ac.on_ground == true);
    CHECK(update_aircraft_from_frame(ac, 11, frame_payload("5da60008e7167c"), 0.0));  // DF11, CA 5: airborne
    CHECK(ac.on_ground == false);
    update_aircraft_from_frame(ac, 5, frame_payload("2A00516D492B80"), 0.0);  // DF5, FS 2
    CHECK(ac.alert);

    // ES position clears the alert (surveillance status 0).
    update_aircraft_from_frame(ac, 17, frame_payload("8da24ce0588be2468e9917208441"), 0.0);
    CHECK(ac.altitude_ft == 26950 && !ac.alert && !ac.spi);

    update_aircraft_from_frame(ac, 17, frame_payload("8da600089914b790385c090c7579"), 0.0);  // TC19 subtype 1
    CHECK(ac.ground_speed_kt && std::abs(*ac.ground_speed_kt - 222.504) < 0.001);  // pyModeS truncates to 222
    CHECK(ac.track_deg && std::abs(*ac.track_deg - 234.881) < 0.01 && ac.vertical_rate_fpm == -1408);
    update_aircraft_from_frame(ac, 17, frame_payload("8DA05F219B06B6AF189400CBC33F"), 0.0);  // TC19 subtype 3
    CHECK(ac.heading_deg == 243.984375 && ac.tas_kt == 375 && !ac.ias_kt && ac.vertical_rate_fpm == -2304);

    update_aircraft_from_frame(ac, 17, frame_payload("8da60008ea09d880015c00138b70"), 0.0);  // TC29, no modes
    CHECK(ac.selected_altitude_ft == 4992 && ac.selected_altitude_source == "MCP");
    CHECK(ac.baro_setting_hpa && std::abs(*ac.baro_setting_hpa - 1016.8) < 1e-9);
    CHECK(!ac.selected_heading_deg && !ac.autopilot_modes);
    update_aircraft_from_frame(ac, 17, frame_payload("8DA05629EA21485CBF3F8CADAEEB"), 0.0);  // TC29, AP VNAV LNAV
    CHECK(ac.selected_altitude_ft == 16992 && ac.selected_heading_deg == 66.796875);
    CHECK(ac.baro_setting_hpa && std::abs(*ac.baro_setting_hpa - 1012.8) < 1e-9);
    CHECK(ac.autopilot_modes == (std::vector<std::string>{"AP", "VNAV", "LNAV"}));
    update_aircraft_from_frame(ac, 17, frame_payload("8da46d95f82100020049b87b1639"), 0.0);  // TC31
    CHECK(ac.adsb_version == 2 && ac.nac_p == 9 && ac.sil == 3);
}

// Comm-B vectors and values from pyModeS.
void test_comm_b() {
    auto mb = [](const std::string& hex) { return static_cast<uint64_t>(frame_payload(hex) >> 24) & ((uint64_t{1} << 56) - 1); };
    const AircraftState none;
    CHECK(infer_bds(mb("A000083E202CC371C31DE0AA1CCF"), none, 12550) == 0x20);
    CHECK(infer_bds(mb("A000029C85E42F313000007047D3"), none, 3300) == 0x40);
    CHECK(infer_bds(mb("A000139381951536E024D4CCF6B5"), none, 30275) == 0x50);
    CHECK(infer_bds(mb("A00004128F39F91A7E27C46ADC21"), none, 5450) == 0x60);
    CHECK(infer_bds(mb("A0000638FA81C10000000081A92F"), none, 9200) == 0);  // 1,7
    CHECK(infer_bds(mb("A0001838E519F33160B9C4A0D5B6"), none, 38000) == 0);  // fits nothing

    // Fits both 5,0 (track 250°, 298 kt GS) and 6,0 (heading 25°): the ADS-B velocity picks one.
    const uint64_t both = 0x891B21257E8454;
    AircraftState ref;
    CHECK(infer_bds(both, ref, std::nullopt) == 0);
    ref.ground_speed_kt = 300;
    ref.track_deg = 252;
    CHECK(infer_bds(both, ref, std::nullopt) == 0x50);
    ref.ground_speed_kt = 250;
    ref.track_deg = 30;
    CHECK(infer_bds(both, ref, std::nullopt) == 0x60);

    AircraftState ac;
    update_aircraft_from_frame(ac, 20, frame_payload("A000083E202CC371C31DE0AA1CCF"), 0.0);
    CHECK(ac.callsign == "KLM1017" && ac.altitude_ft == 12550);
    update_aircraft_from_frame(ac, 20, frame_payload("A000029C85E42F313000007047D3"), 0.0);
    CHECK(ac.selected_altitude_ft == 3008 && ac.selected_altitude_source == "MCP");
    CHECK(ac.baro_setting_hpa && std::abs(*ac.baro_setting_hpa - 1020.0) < 1e-9);
    update_aircraft_from_frame(ac, 20, frame_payload("A000139381951536E024D4CCF6B5"), 0.0);
    CHECK(ac.roll_deg == 2.109375 && ac.tas_kt == 424 && !ac.ground_speed_kt && !ac.track_deg);
    update_aircraft_from_frame(ac, 20, frame_payload("A00004128F39F91A7E27C46ADC21"), 0.0);
    CHECK(ac.heading_deg == 42.71484375 && ac.ias_kt == 252 && ac.mach && std::abs(*ac.mach - 0.42) < 1e-9);
}

void test_frame_summary() {
    auto summary = [](unsigned df, const std::string& hex, bool crc_ok = true) {
        return frame_summary(DecodedFrameView{0, df, crc_ok, true, crc_ok, 0, frame_payload(hex), -1, 112, 0.f});
    };
    CHECK(summary(4, "20000516109922") == "Altitude · 7150 ft");
    CHECK(summary(4, "21000516109922") == "Altitude · 7150 ft · ground");
    CHECK(summary(5, "2A00516D492B80") == "Identity · squawk 0356 · alert");
    CHECK(summary(11, "5da60008e7167c") == "All-call");
    CHECK(summary(17, "8da600089914b790385c090c7579") == "Velocity · 223 kt GS · trk 235° · -1408 fpm");
    CHECK(summary(17, "8DA05F219B06B6AF189400CBC33F") == "Velocity · hdg 244° · 375 kt TAS · -2304 fpm");
    CHECK(summary(17, "8DA05629EA21485CBF3F8CADAEEB") == "Target state · sel 16992 ft · sel hdg 67° · AP · VNAV · LNAV");
    CHECK(summary(17, "8da60008f8132006005ab8a5a794") == "Operational status · ADS-B v2 · NACp 10 · SIL 3");
    CHECK(summary(20, "A000139381951536E024D4CCF6B5") == "Comm-B altitude · 30275 ft · BDS 5,0 · roll 2.1° · 424 kt TAS");
    CHECK(summary(20, "A00004128F39F91A7E27C46ADC21") == "Comm-B altitude · 5450 ft · BDS 6,0 · hdg 43° · 252 kt IAS · M0.420");
    CHECK(summary(20, "A0000638FA81C10000000081A92F") == "Comm-B altitude · 9200 ft");
    CHECK(summary(17, "8D4840D6E12AAA000000003CF5CE") == "Emergency status · squawk 7700 · emergency");
    CHECK(summary(17, "8D4840D6202CC371C32CE0576098") == "Ident · KLM1023");
    CHECK(summary(4, "20000516109922", false).empty());
}

// Config with the first `n` aircraft of the shipped scenario file.
adsb_sim::Config scenario_config(size_t n) {
    adsb_sim::Config cfg;
    cfg.aircraft = adsb_sim::load_scenario(ADSB_SIM_SCENARIO_DIR "/boston.json");
    cfg.aircraft.resize(std::min(n, cfg.aircraft.size()));
    return cfg;
}

std::vector<int16_t> generate_all(const adsb_sim::Config& cfg, size_t total, size_t chunk) {
    adsb_sim::Simulator sim(cfg);
    std::vector<int16_t> out(2 * total);
    for (size_t done = 0; done < total;) {
        const size_t n = std::min(chunk, total - done);
        sim.generate(std::span(out).subspan(2 * done, 2 * n));
        done += n;
    }
    return out;
}

struct Slice {
    double start;  // sample index (fractional) of the first preamble pulse's 50% rising crossing
    adsb_sim::Frame frame;
};

std::vector<double> magnitudes(const std::vector<int16_t>& iq) {
    std::vector<double> m(iq.size() / 2);
    for (size_t s = 0; s < m.size(); s++) m[s] = std::hypot(static_cast<double>(iq[2 * s]), static_cast<double>(iq[2 * s + 1]));
    return m;
}

// Fractional sample position where mag crosses `level` between samples s-1 and s.
double crossing(const std::vector<double>& mag, size_t s, double level) {
    return static_cast<double>(s - 1) + (level - mag[s - 1]) / (mag[s] - mag[s - 1]);
}

// Reference receiver for shaped pulses at arbitrary sub-sample alignment: a rising edge through
// `threshold` gives the frame start to a fraction of a sample, chips are read by linearly interpolating
// the magnitude at their centres, and a PPM bit is whichever half-symbol is larger. The CRC (checked
// here) is what tells a real frame from a false start.
std::vector<Slice> slice_frames(const std::vector<int16_t>& iq, uint32_t rate, double threshold) {
    const auto mag = magnitudes(iq);
    const size_t n = mag.size();
    const double samples_per_chip = rate / 2e6;
    auto at = [&](double t) {
        const auto i = static_cast<size_t>(t);
        return mag[i] + (t - static_cast<double>(i)) * (mag[i + 1] - mag[i]);
    };

    std::vector<Slice> out;
    for (size_t s = 1; s < n; s++) {
        if (!(mag[s - 1] < threshold && mag[s] >= threshold)) continue;
        const double t0 = crossing(mag, s, threshold);
        if (t0 + (16 + 224 + 1) * samples_per_chip >= static_cast<double>(n - 1)) break;
        auto chip = [&](unsigned c) { return at(t0 + (c + 0.5) * samples_per_chip); };

        adsb_sim::Frame f;
        f.num_bits = 112;
        for (unsigned bit = 0; bit < f.num_bits; bit++) {
            if (chip(16 + 2 * bit) > chip(16 + 2 * bit + 1)) f.bytes[bit / 8] |= static_cast<uint8_t>(1 << (7 - bit % 8));
            if (bit == 4) f.num_bits = (f.bytes[0] >> 3) == 11 ? 56 : 112;
        }
        if (adsb_sim::crc24(f.bytes.data(), f.num_bits / 8) != 0) continue;
        out.push_back({t0, f});
        s = static_cast<size_t>(t0 + (16 + 2 * f.num_bits) * samples_per_chip);
    }
    return out;
}

void test_modulation_roundtrip() {
    // 8 MHz exercises 2x oversampling; at the 2 MHz minimum a chip is a single, heavily filtered sample,
    // which this simple reference receiver cannot slice (generation there is covered by test_determinism).
    for (uint32_t rate : {2'400'000u, 3'200'000u, 8'000'000u}) {
        adsb_sim::Config cfg = scenario_config(1);
        cfg.sample_rate_hz = rate;
        cfg.snr_db = 30;
        constexpr size_t kSeconds = 3;
        const auto iq = generate_all(cfg, kSeconds * rate, 4096);
        const auto frames = slice_frames(iq, rate, 0.25 * 32767.0);  // half the pulse amplitude (0.5 full scale)

        std::map<unsigned, unsigned> by_tc;  // DF17 type codes; DF11 counted under key 0
        AircraftState ac;
        const auto& spec = cfg.aircraft[0];
        for (const auto& sl : frames) {
            const unsigned df = sl.frame.bytes[0] >> 3;
            const uint32_t icao = (sl.frame.bytes[1] << 16) | (sl.frame.bytes[2] << 8) | sl.frame.bytes[3];
            CHECK(icao == spec.icao);
            if (df == 11) { by_tc[0]++; continue; }
            CHECK(df == 17);
            by_tc[sl.frame.bytes[4] >> 3]++;
            update_aircraft_from_me(ac, payload_of(sl.frame), sl.start / rate);
        }
        std::printf("rate %u: %zu frames (squitter %u, ident %u, pos %u, vel %u)\n", rate, frames.size(), by_tc[0], by_tc[4],
                    by_tc[11], by_tc[19]);
        // ~1 Hz squitters, one ident in the first second, ~2 Hz position and velocity over 3 s.
        CHECK(by_tc[0] >= 2 && by_tc[4] >= 1 && by_tc[11] >= 4 && by_tc[19] >= 4);
        CHECK(ac.callsign && *ac.callsign == "SIM0001");
        CHECK(ac.lat && ac.lon && std::abs(*ac.lat - spec.lat_deg) < 0.02);
        CHECK(ac.ground_speed_kt && std::abs(*ac.ground_speed_kt - spec.ground_speed_kt) < 1.0);
    }
}

void test_scenario_parsing() {
    using adsb_sim::parse_scenario;

    const auto shipped = adsb_sim::load_scenario(ADSB_SIM_SCENARIO_DIR "/boston.json");
    CHECK(shipped.size() == 5);
    CHECK(shipped[0].icao == 0xA1B2C3 && shipped[0].callsign == "SIM0001");
    CHECK(shipped[0].lat_deg == 42.3601 && shipped[0].lon_deg == -71.0589 && shipped[0].alt_ft == 35000);
    CHECK(shipped[1].vrate_fpm == -1500 && shipped[1].amplitude == 0.7 && shipped[1].track_deg == 200);

    // Optional fields take their defaults; hex is case-insensitive.
    const auto min = parse_scenario(R"({"aircraft":[{"icao":"a1b2c3","lat_deg":1,"lon_deg":-2,"alt_ft":0,
                                                     "ground_speed_kt":100,"track_deg":10}]})");
    CHECK(min.size() == 1 && min[0].icao == 0xA1B2C3 && min[0].callsign.empty() && min[0].vrate_fpm == 0 &&
          min[0].amplitude == 1.0);

    const std::string ok = R"({"icao":"ABCDEF","lat_deg":1,"lon_deg":2,"alt_ft":3,"ground_speed_kt":4,"track_deg":5})";
    auto with = [&](const std::string& extra_or_override) {  // ok with `extra_or_override` fields appended
        return "{\"aircraft\":[" + ok.substr(0, ok.size() - 1) + "," + extra_or_override + "}]}";
    };
    CHECK(!throws_invalid([&] { parse_scenario(with(R"("callsign":"AB 12")")); }));
    CHECK(parse_scenario(with(R"("period_jitter":0.25)"))[0].period_jitter == 0.25);
    CHECK(throws_invalid([&] { parse_scenario(with(R"("period_jitter":-0.1)")); }));
    CHECK(throws_invalid([&] { parse_scenario(with(R"("period_jitter":0.9)")); }));

    CHECK(throws_invalid([] { adsb_sim::load_scenario("/nonexistent/scenario.json"); }));
    CHECK(throws_invalid([] { parse_scenario("not json"); }));
    CHECK(throws_invalid([] { parse_scenario("[]"); }));
    CHECK(throws_invalid([] { parse_scenario(R"({"aircraft":[]})"); }));
    CHECK(throws_invalid([&] { parse_scenario(R"({"aircraft":[)" + ok + R"(],"extra":1})"); }));
    CHECK(throws_invalid([&] { parse_scenario(R"({"aircraft":[)" + ok + "," + ok + "]}"); }));  // duplicate icao
    CHECK(throws_invalid([] { parse_scenario(R"({"aircraft":[{"icao":"ABCDEF"}]})"); }));       // missing fields
    CHECK(throws_invalid([&] { parse_scenario(with(R"("bogus":1)")); }));
    CHECK(throws_invalid([&] { parse_scenario(with(R"("icao":"XYZ123")")); }));
    CHECK(throws_invalid([&] { parse_scenario(with(R"("icao":"ABCDE")")); }));
    CHECK(throws_invalid([&] { parse_scenario(with(R"("icao":123456)")); }));
    CHECK(throws_invalid([&] { parse_scenario(with(R"("callsign":"lowercase")")); }));
    CHECK(throws_invalid([&] { parse_scenario(with(R"("callsign":"TOOLONG123")")); }));
    CHECK(throws_invalid([&] { parse_scenario(with(R"("lat_deg":91)")); }));
    CHECK(throws_invalid([&] { parse_scenario(with(R"("lon_deg":-181)")); }));
    CHECK(throws_invalid([&] { parse_scenario(with(R"("alt_ft":60000)")); }));
    CHECK(throws_invalid([&] { parse_scenario(with(R"("ground_speed_kt":-1)")); }));
    CHECK(throws_invalid([&] { parse_scenario(with(R"("amplitude":0)")); }));
    CHECK(throws_invalid([&] { parse_scenario(with(R"("amplitude":1.5)")); }));
    CHECK(throws_invalid([&] { parse_scenario(with(R"("track_deg":"north")")); }));
}

// Position messages are nominally 0.5 s apart; measure the actual spacing of
// one aircraft's squitters-free stream via the reference slicer.
double position_interval_spread(double jitter) {
    adsb_sim::Config cfg = scenario_config(1);
    cfg.aircraft[0].period_jitter = jitter;
    constexpr uint32_t rate = 2'400'000;
    cfg.sample_rate_hz = rate;
    const auto iq = generate_all(cfg, 30ull * rate, 65536);
    std::vector<double> starts;
    for (const auto& sl : slice_frames(iq, rate, 0.25 * 32767.0))
        if ((sl.frame.bytes[0] >> 3) == 17 && (sl.frame.bytes[4] >> 3) == 11) starts.push_back(sl.start / rate);
    CHECK(starts.size() > 40);  // ~60 expected in 30 s; guards against a vacuous spread
    double lo = 1e9, hi = 0;
    for (size_t i = 1; i < starts.size(); i++) {
        const double d = starts[i] - starts[i - 1];
        if (d > 0.75) continue;  // the reference receiver missed a frame in between
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    std::printf("jitter %.2f: interval spread %.9f s over %zu positions\n", jitter, hi - lo, starts.size());
    return hi - lo;
}

void test_period_jitter() {
    // 0 jitter: perfectly periodic (to within a sample); 0.2 jitter: intervals
    // spread across roughly +/-20% of 0.5 s but never beyond.
    CHECK(position_interval_spread(0.0) < 3e-6);  // only the random sub-sample start (+/-1 sample) and edge errors remain
    const double spread = position_interval_spread(0.2);
    CHECK(spread > 0.1 && spread <= 0.2 + 1e-5);
}

// Pulse shape, measured on a clean, high-rate capture (no oversampling, so nothing but the transmit
// waveform is in the way): 50% edge crossings of the four preamble pulses.
struct PreambleEdges {
    double rise[4], fall[4];  // seconds, relative to the first rising edge
    double rise_10_90;        // first pulse's rising edge, seconds
};

PreambleEdges measure_preamble(const adsb_sim::Config& cfg) {
    const uint32_t rate = cfg.sample_rate_hz;
    const double amp = 0.5 * 32767.0 * cfg.aircraft[0].amplitude;
    const auto iq = generate_all(cfg, rate / 2, 65536);  // first message is at most 0.5 s in
    const auto mag = magnitudes(iq);
    size_t s = 1;
    while (s < mag.size() && !(mag[s - 1] < 0.5 * amp && mag[s] >= 0.5 * amp)) s++;
    CHECK(s < mag.size());
    PreambleEdges e{};
    const double first = crossing(mag, s, 0.5 * amp);
    e.rise_10_90 = 0;
    size_t lo = s;  // walk back to the 10% point and forward to the 90% point of the first edge
    while (lo > 1 && mag[lo - 1] > 0.1 * amp) lo--;
    size_t hi = s;
    while (hi + 1 < mag.size() && mag[hi] < 0.9 * amp) hi++;
    e.rise_10_90 = (crossing(mag, hi, 0.9 * amp) - crossing(mag, lo, 0.1 * amp)) / rate;

    int r = 0, f = 0;
    bool high = false;
    for (size_t i = s; i + 1 < mag.size() && f < 4; i++) {
        if (!high && mag[i - 1] < 0.5 * amp && mag[i] >= 0.5 * amp) {
            e.rise[r++] = (crossing(mag, i, 0.5 * amp) - first) / rate;
            high = true;
        } else if (high && mag[i - 1] >= 0.5 * amp && mag[i] < 0.5 * amp) {
            e.fall[f++] = (crossing(mag, i, 0.5 * amp) - first) / rate;
            high = false;
        }
        if (r > 4) break;
    }
    CHECK(r >= 4 && f == 4);
    return e;
}

void test_pulse_shape() {
    adsb_sim::Config cfg = scenario_config(1);
    cfg.sample_rate_hz = 50'000'000;
    cfg.snr_db = 100;  // effectively noise-free
    cfg.pulse_jitter_s = 0;
    cfg.pulse_rise_s = 80e-9;
    cfg.pulse_fall_s = 120e-9;
    const auto clean = measure_preamble(cfg);
    // Preamble pulses start at 0, 1.0, 3.5 and 4.5 us and are 0.5 us wide, edges centred on those instants.
    const double rise_nominal[4] = {0, 1.0e-6, 3.5e-6, 4.5e-6};
    for (int i = 0; i < 4; i++) {
        CHECK(std::abs(clean.rise[i] - rise_nominal[i]) < 5e-9);
        CHECK(std::abs(clean.fall[i] - (rise_nominal[i] + 0.5e-6)) < 5e-9);
    }
    CHECK(std::abs(clean.rise_10_90 - 0.8 * 80e-9) < 4e-9);  // linear edge: 10-90% is 0.8 of the ramp

    cfg.pulse_jitter_s = 25e-9;
    const auto jittery = measure_preamble(cfg);
    double max_dev = 0;
    for (int i = 0; i < 4; i++) {
        max_dev = std::max({max_dev, std::abs(jittery.rise[i] - rise_nominal[i]), std::abs(jittery.fall[i] - rise_nominal[i] - 0.5e-6)});
        // Each edge moves by <= 25 ns, and the reference edge itself by <= 25 ns.
        CHECK(std::abs(jittery.rise[i] - rise_nominal[i]) <= 50e-9 + 5e-9);
        CHECK(std::abs(jittery.fall[i] - rise_nominal[i] - 0.5e-6) <= 50e-9 + 5e-9);
    }
    CHECK(max_dev > 5e-9);  // the timing error is really applied
}

void test_determinism() {
    adsb_sim::Config cfg = scenario_config(5);
    cfg.seed = 42;
    constexpr size_t kTotal = 300'000;
    const auto a = generate_all(cfg, kTotal, 360);
    CHECK(a == generate_all(cfg, kTotal, 360));
    CHECK(a == generate_all(cfg, kTotal, 1000));  // independent of chunking
    CHECK(a == generate_all(cfg, kTotal, 1));
    int16_t peak = 0;
    for (int16_t v : a) peak = std::max<int16_t>(peak, static_cast<int16_t>(std::abs(v)));
    CHECK(peak > 8000);  // the comparison above must not be vacuous: a message is on the air

    // Same, at rates that need no oversampling (20 MHz) and at the 2 MHz minimum.
    for (uint32_t rate : {2'000'000u, 20'000'000u}) {
        adsb_sim::Config hi = scenario_config(5);
        hi.seed = 7;
        hi.sample_rate_hz = rate;
        const size_t total = static_cast<size_t>(rate) * 6 / 10;  // every emitter but the ident ones starts within 0.5 s
        const auto x = generate_all(hi, total, 4096);
        CHECK(x == generate_all(hi, total, 65536));
        CHECK(x == generate_all(hi, total, 999));
        int16_t pk = 0;
        for (int16_t v : x) pk = std::max<int16_t>(pk, static_cast<int16_t>(std::abs(v)));
        CHECK(pk > 8000);
    }

    cfg.seed = 43;
    CHECK(a != generate_all(cfg, kTotal, 360));

    adsb_sim::Config bad = scenario_config(1);
    bad.sample_rate_hz = 1'000'000;
    CHECK(throws_invalid([&] { adsb_sim::Simulator s(bad); }));
    adsb_sim::Config empty;  // no aircraft
    CHECK(throws_invalid([&] { adsb_sim::Simulator s(empty); }));
}

void test_noise_level() {
    // At 10 dB the noise is far above the pulses' duty-weighted power (~0.03%
    // duty) but still well inside int16 range, so the total RMS should match the
    // noise-only target closely.
    adsb_sim::Config cfg = scenario_config(1);
    cfg.snr_db = 10;
    const auto iq = generate_all(cfg, 1'000'000, 4096);
    double sum = 0;
    for (int16_t v : iq) sum += static_cast<double>(v) * v;
    const double rms_per_component = std::sqrt(sum / iq.size());
    const double expected = 0.5 * 32767.0 / std::sqrt(2.0 * std::pow(10.0, 1.0));
    CHECK(std::abs(rms_per_component / expected - 1.0) < 0.02);
}

}  // namespace

// The noise is counter-based (src/sim/sim_kernels.ispc): a pure function of (seed, sample index), so it
// must be Gaussian, white, and I/Q-independent. (That it does not depend on how calls are chunked --
// including chunks larger than the simulator's internal sub-blocks -- is covered by test_determinism.)
void test_noise_statistics() {
    adsb_sim::Config cfg = scenario_config(1);
    cfg.snr_db = 10;
    cfg.aircraft[0].amplitude = 1e-4;  // noise only: pulses would fatten the tails being measured
    constexpr size_t kN = 2'000'000;
    const auto iq = generate_all(cfg, kN, 65536);
    const double sigma = 0.5 * 32767.0 / std::sqrt(2.0 * std::pow(10.0, 1.0));
    std::vector<double> i(kN), q(kN);
    for (size_t k = 0; k < kN; k++) {
        i[k] = iq[2 * k] / sigma;
        q[k] = iq[2 * k + 1] / sigma;
    }
    auto moment = [&](const std::vector<double>& x, int p) {
        double m = 0;
        for (double v : x) m += std::pow(v, p);
        return m / static_cast<double>(x.size());
    };
    auto corr = [&](const std::vector<double>& x, const std::vector<double>& y, size_t lag) {
        double c = 0;
        for (size_t k = 0; k + lag < kN; k++) c += x[k] * y[k + lag];
        return c / static_cast<double>(kN - lag);
    };
    for (const auto* x : {&i, &q}) {
        CHECK(std::abs(moment(*x, 1)) < 0.005);
        CHECK(std::abs(moment(*x, 2) - 1.0) < 0.01);
        CHECK(std::abs(moment(*x, 4) - 3.0) < 0.06);  // Gaussian kurtosis (a truncated or two-point tail would miss)
        size_t tail = 0;
        for (double v : *x) tail += std::abs(v) > 3.0;
        CHECK(std::abs(static_cast<double>(tail) / kN / 0.0026998 - 1.0) < 0.06);
        for (size_t lag : {1, 2, 3, 16, 1000}) CHECK(std::abs(corr(*x, *x, lag)) < 0.006);
    }
    for (size_t lag : {0, 1, 2}) CHECK(std::abs(corr(i, q, lag)) < 0.006);
}

int main() {
    test_crc();
    test_encoders_crc_and_decode();
    test_squawk_and_category();
    test_reply_and_state_decodes();
    test_comm_b();
    test_frame_summary();
    test_scenario_parsing();
    test_modulation_roundtrip();
    test_period_jitter();
    test_determinism();
    test_noise_level();
    test_noise_statistics();
    std::printf(g_failures ? "%d FAILURE(S)\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
