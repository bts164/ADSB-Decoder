#include "decode/aircraft.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include <nlohmann/json.hpp>

#include "common/constants.h"

std::unordered_map<uint32_t, AircraftState>& aircraft_table() {
    static std::unordered_map<uint32_t, AircraftState> table;
    return table;
}

namespace {

// Bits a..b of the ME field (or the Comm-B MB field, which sits in the same place), numbered from 1 as the
// standards do. At most 32 bits.
unsigned me_bits(uint64_t me56, int a, int b) {
    return static_cast<unsigned>((me56 >> (56 - b)) & ((uint64_t{1} << (b - a + 1)) - 1));
}

// Bits a..b of the frame (first bit in bit 111 of `payload`), numbered from 1.
unsigned frame_bits(unsigned __int128 payload, int a, int b) {
    return static_cast<unsigned>(payload >> (112 - b)) & ((1u << (b - a + 1)) - 1);
}

uint64_t me_field(unsigned __int128 payload) {
    return static_cast<uint64_t>((payload >> 24) & ((static_cast<unsigned __int128>(1) << 56) - 1));
}

std::optional<std::string> decode_callsign(uint64_t me56) {
    auto ch = [](unsigned v) -> char {
        if (v >= 1 && v <= 26) return static_cast<char>('A' + (v - 1));
        if (v == 32) return ' ';
        if (v >= 48 && v <= 57) return static_cast<char>('0' + (v - 48));
        return '?';
    };
    std::string s;
    for (int i = 0; i < 8; i++) {
        unsigned v = (me56 >> (42 - 6 * i)) & 0x3F;
        s += ch(v);
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.empty()) return std::nullopt;
    return s;
}

// 12-bit AC-style altitude field from an airborne-position ME (TC 9-18).
// Only the Q=1 (25ft-increment) encoding is handled; Q=0 is legacy
// Gillham/Mode-C encoding, rare in modern ADS-B and not decoded here.
std::optional<int> decode_alt12(unsigned alt_field) {
    unsigned q = (alt_field >> 4) & 0x1;
    if (!q) return std::nullopt;
    unsigned n = ((alt_field >> 5) << 4) | (alt_field & 0xF);
    return static_cast<int>(n) * 25 - 1000;
}

// 13-bit AC altitude field of DF0/4/16/20 (bits 20-32): the 12-bit field above plus an M bit (metric) at
// index 6. Metric altitudes are rare and left unset, like Gillham ones.
std::optional<int> decode_alt13(unsigned ac13) {
    if (ac13 & 0x40) return std::nullopt;
    return decode_alt12(((ac13 >> 7) << 6) | (ac13 & 0x3F));
}

// Number of longitude zones for a given latitude, per the standard CPR
// formula (RTCA DO-260 / ICAO Annex 10). NZ=15 for the 360/4NZ latitude
// zone size ADS-B uses.
int cpr_nl(double lat) {
    if (lat == 0) return 59;
    if (lat == 87.0 || lat == -87.0) return 2;
    if (std::abs(lat) > 87.0) return 1;
    constexpr double kNz = 15.0;
    double a = 1 - std::cos(kPi / (2 * kNz));
    double b = std::cos(kPi / 180.0 * std::abs(lat));
    double val = 1 - a / (b * b);
    val = std::clamp(val, -1.0, 1.0);
    return static_cast<int>(std::floor(2 * kPi / std::acos(val)));
}

// Global CPR decode: resolves an unambiguous lat/lon from one even and one
// odd airborne-position CPR report (raw 17-bit fields), given which of the
// two is the more recent (used as the position/zone-count reference).
// Standard algorithm; see e.g. "The 1090MHz Riddle" sec. on CPR decoding.
std::optional<std::pair<double, double>> cpr_resolve_global(uint32_t lat_cpr_e_raw, uint32_t lon_cpr_e_raw,
                                                              uint32_t lat_cpr_o_raw, uint32_t lon_cpr_o_raw,
                                                              bool even_is_newer) {
    double cprlat_e = lat_cpr_e_raw / 131072.0;
    double cprlon_e = lon_cpr_e_raw / 131072.0;
    double cprlat_o = lat_cpr_o_raw / 131072.0;
    double cprlon_o = lon_cpr_o_raw / 131072.0;

    auto pmod = [](double a, double b) {
        double r = std::fmod(a, b);
        return r < 0 ? r + b : r;
    };

    constexpr double d_lat_even = 360.0 / 60.0;
    constexpr double d_lat_odd = 360.0 / 59.0;

    double j = std::floor(59 * cprlat_e - 60 * cprlat_o + 0.5);
    double lat_even = d_lat_even * (pmod(j, 60.0) + cprlat_e);
    double lat_odd = d_lat_odd * (pmod(j, 59.0) + cprlat_o);
    if (lat_even >= 270) lat_even -= 360;
    if (lat_odd >= 270) lat_odd -= 360;

    if (cpr_nl(lat_even) != cpr_nl(lat_odd)) return std::nullopt;

    double lat, lon;
    if (even_is_newer) {
        lat = lat_even;
        int nl = cpr_nl(lat_even);
        int ni = std::max(nl, 1);
        double m = std::floor(cprlon_e * (nl - 1) - cprlon_o * nl + 0.5);
        lon = (360.0 / ni) * (pmod(m, static_cast<double>(ni)) + cprlon_e);
    } else {
        lat = lat_odd;
        int nl = cpr_nl(lat_odd);
        int ni = std::max(nl - 1, 1);
        double m = std::floor(cprlon_e * (nl - 1) - cprlon_o * nl + 0.5);
        lon = (360.0 / ni) * (pmod(m, static_cast<double>(ni)) + cprlon_o);
    }
    if (lon > 180) lon -= 360;
    if (lat < -90 || lat > 90) return std::nullopt;
    return std::make_pair(lat, lon);
}

// 13-bit Mode A identity field, bits C1 A1 C2 A2 C4 A4 X B1 D1 B2 D2 B4 D4
// (MSB first), as the four octal digits ABCD.
std::string decode_squawk(unsigned id13) {
    auto bit = [id13](int i) { return (id13 >> (12 - i)) & 0x1; };
    const unsigned a = bit(5) << 2 | bit(3) << 1 | bit(1);
    const unsigned b = bit(11) << 2 | bit(9) << 1 | bit(7);
    const unsigned c = bit(4) << 2 | bit(2) << 1 | bit(0);
    const unsigned d = bit(12) << 2 | bit(10) << 1 | bit(8);
    return {static_cast<char>('0' + a), static_cast<char>('0' + b), static_cast<char>('0' + c),
            static_cast<char>('0' + d)};
}

template <typename T, typename V>
void set_if_changed(T& field, V&& value, bool& changed) {
    if (field == value) return;
    field = std::forward<V>(value);
    changed = true;
}

// Flight status (bits 6-8 of DF4/5/20/21). 6 and 7 are unassigned.
void apply_flight_status(AircraftState& ac, unsigned fs, bool& changed) {
    if (fs >= 6) return;
    if (fs <= 3) set_if_changed(ac.on_ground, fs == 1 || fs == 3, changed);  // 4 and 5 are either
    set_if_changed(ac.alert, fs >= 2 && fs <= 4, changed);
    set_if_changed(ac.spi, fs == 4 || fs == 5, changed);
}

void handle_position(AircraftState& ac, uint64_t me56, double t, bool& changed) {
    unsigned alt_field = (me56 >> 36) & 0xFFF;
    unsigned f_bit = (me56 >> 34) & 0x1;  // 0=even, 1=odd
    uint32_t lat_cpr = (me56 >> 17) & 0x1FFFF;
    uint32_t lon_cpr = me56 & 0x1FFFF;

    // Surveillance status: 1 is a permanent alert (emergency), 2 a temporary one (squawk changed).
    const unsigned ss = me_bits(me56, 6, 7);
    set_if_changed(ac.alert, ss == 1 || ss == 2, changed);
    set_if_changed(ac.spi, ss == 3, changed);

    auto alt = decode_alt12(alt_field);
    if (alt && ac.altitude_ft != alt) {
        ac.altitude_ft = alt;
        changed = true;
    }

    auto& slot = f_bit ? ac.cpr_odd : ac.cpr_even;
    slot = {lat_cpr, lon_cpr, t, true};

    if (ac.cpr_even.have && ac.cpr_odd.have && std::abs(ac.cpr_even.t - ac.cpr_odd.t) <= 10.0) {
        bool even_is_newer = ac.cpr_even.t >= ac.cpr_odd.t;
        auto pos = cpr_resolve_global(ac.cpr_even.lat_cpr, ac.cpr_even.lon_cpr, ac.cpr_odd.lat_cpr, ac.cpr_odd.lon_cpr,
                                       even_is_newer);
        if (pos && (ac.lat != pos->first || ac.lon != pos->second)) {
            ac.lat = pos->first;
            ac.lon = pos->second;
            changed = true;
        }
    }
}

// Ground speed and track, from velocity subtypes 1-2.
void handle_ground_velocity(AircraftState& ac, uint64_t me56, int scale, bool& changed) {
    unsigned ew_sign = (me56 >> 42) & 0x1;
    unsigned ew_raw = (me56 >> 32) & 0x3FF;
    unsigned ns_sign = (me56 >> 31) & 0x1;
    unsigned ns_raw = (me56 >> 21) & 0x3FF;

    if (ew_raw != 0 && ns_raw != 0) {
        int vx = static_cast<int>(ew_raw - 1) * scale * (ew_sign ? -1 : 1);
        int vy = static_cast<int>(ns_raw - 1) * scale * (ns_sign ? -1 : 1);
        double speed = std::sqrt(static_cast<double>(vx) * vx + static_cast<double>(vy) * vy);
        double track = std::atan2(static_cast<double>(vx), static_cast<double>(vy)) * 180.0 / kPi;
        if (track < 0) track += 360;
        if (!ac.ground_speed_kt || std::abs(*ac.ground_speed_kt - speed) > 0.05) {
            ac.ground_speed_kt = speed;
            changed = true;
        }
        if (!ac.track_deg || std::abs(*ac.track_deg - track) > 0.05) {
            ac.track_deg = track;
            changed = true;
        }
    }
}

// Magnetic heading and IAS or TAS, from velocity subtypes 3-4.
void handle_air_velocity(AircraftState& ac, uint64_t me56, int scale, bool& changed) {
    if (me_bits(me56, 14, 14)) set_if_changed(ac.heading_deg, me_bits(me56, 15, 24) * 360.0 / 1024, changed);
    if (const unsigned speed = me_bits(me56, 26, 35))
        set_if_changed(me_bits(me56, 25, 25) ? ac.tas_kt : ac.ias_kt, static_cast<int>(speed - 1) * scale, changed);
}

// Airborne velocity (TC 19): ground speed and track (subtypes 1-2) or magnetic heading and airspeed
// (subtypes 3-4), plus vertical rate. Subtypes 2 and 4 are supersonic, in 4 kt steps.
void handle_velocity(AircraftState& ac, uint64_t me56, bool& changed) {
    const unsigned st = me_bits(me56, 6, 8);
    if (st < 1 || st > 4) return;
    const int speed_scale = st == 2 || st == 4 ? 4 : 1;
    if (st <= 2) handle_ground_velocity(ac, me56, speed_scale, changed);
    else handle_air_velocity(ac, me56, speed_scale, changed);

    unsigned vr_sign = (me56 >> 19) & 0x1;
    unsigned vr_raw = (me56 >> 10) & 0x1FF;
    if (vr_raw != 0) {
        int vr = static_cast<int>(vr_raw - 1) * 64 * (vr_sign ? -1 : 1);
        if (ac.vertical_rate_fpm != vr) {
            ac.vertical_rate_fpm = vr;
            changed = true;
        }
    }
}

// Target state and status (TC 29), version 2 (subtype 1) only: version 1's layout differs.
void handle_target_state(AircraftState& ac, uint64_t me56, bool& changed) {
    if (me_bits(me56, 6, 7) != 1) return;
    if (const unsigned alt = me_bits(me56, 10, 20)) {
        set_if_changed(ac.selected_altitude_ft, static_cast<int>(alt - 1) * 32, changed);
        set_if_changed(ac.selected_altitude_source, std::string(me_bits(me56, 9, 9) ? "FMS" : "MCP"), changed);
    }
    if (const unsigned baro = me_bits(me56, 21, 29)) set_if_changed(ac.baro_setting_hpa, 800 + (baro - 1) * 0.8, changed);
    if (me_bits(me56, 30, 30)) set_if_changed(ac.selected_heading_deg, me_bits(me56, 31, 39) * 180.0 / 256, changed);
    // The mode bits are only meaningful when their status bit is set.
    if (me_bits(me56, 47, 47)) {
        std::vector<std::string> modes;
        if (me_bits(me56, 48, 48)) modes.emplace_back("AP");
        if (me_bits(me56, 49, 49)) modes.emplace_back("VNAV");
        if (me_bits(me56, 50, 50)) modes.emplace_back("ALT");
        if (me_bits(me56, 52, 52)) modes.emplace_back("APP");
        if (me_bits(me56, 54, 54)) modes.emplace_back("LNAV");
        set_if_changed(ac.autopilot_modes, std::move(modes), changed);
    }
}

// Operational status (TC 31). NACp and SIL are only defined from version 1 on.
void handle_operational_status(AircraftState& ac, uint64_t me56, bool& changed) {
    const int version = static_cast<int>(me_bits(me56, 41, 43));
    set_if_changed(ac.adsb_version, version, changed);
    if (version < 1) return;
    set_if_changed(ac.nac_p, static_cast<int>(me_bits(me56, 45, 48)), changed);
    set_if_changed(ac.sil, static_cast<int>(me_bits(me56, 51, 52)), changed);
}

// ---- Comm-B (DF20/21 MB field) registers. The checks are pyModeS's (bds10.py ... bds60.py). ----

// A field whose status bit is clear must be all zeros; if it isn't, the MB field isn't this register.
bool wrong_status(uint64_t mb, int status_bit, int a, int b) {
    return !me_bits(mb, status_bit, status_bit) && me_bits(mb, a, b) != 0;
}

// Two's-complement-style field: sign bit, then `b - a + 1` value bits.
int signed_field(uint64_t mb, int sign_bit, int a, int b) {
    const int v = static_cast<int>(me_bits(mb, a, b));
    return me_bits(mb, sign_bit, sign_bit) ? v - (1 << (b - a + 1)) : v;
}

double angle_diff(double a, double b) {
    const double d = std::fmod(std::abs(a - b), 360.0);
    return d > 180 ? 360 - d : d;
}

// International Standard Atmosphere, for checking BDS 6,0's IAS against its Mach number (pyModeS aero.py).
double mach_to_cas_kt(double mach, double alt_ft) {
    constexpr double kR = 287.05287, kP0 = 101325, kRho0 = 1.225;
    const double h = alt_ft * 0.3048;
    const double temp = std::max(288.15 - 0.0065 * h, 216.65);
    const double rho = kRho0 * std::pow(temp / 288.15, 4.256848042) * std::exp(-std::max(0.0, h - 11000) / 6341.552161);
    const double p = rho * kR * temp;
    const double tas = mach * std::sqrt(1.4 * kR * temp);
    const double qdyn = p * (std::pow(1 + rho * tas * tas / (7 * p), 3.5) - 1);
    return std::sqrt(7 * kP0 / kRho0 * (std::pow(qdyn / kP0 + 1, 2.0 / 7) - 1)) / 0.514444;
}

// Registers that aren't decoded, but whose rules are checked so a field matching one of them as well as a
// decoded register is left alone rather than guessed at.
bool is_bds10(uint64_t mb) {
    if (me_bits(mb, 1, 8) != 0x10 || me_bits(mb, 10, 14) != 0) return false;
    const unsigned ovc = me_bits(mb, 17, 23);
    return me_bits(mb, 15, 15) ? ovc >= 5 : ovc <= 4;
}
bool is_bds17(uint64_t mb) { return me_bits(mb, 25, 56) == 0 && me_bits(mb, 7, 7); }  // 7: supports 2,0
bool is_bds30(uint64_t mb) {
    return me_bits(mb, 1, 8) == 0x30 && me_bits(mb, 29, 30) != 3 && me_bits(mb, 16, 22) < 48;
}

// 2,0: aircraft identification.
bool is_bds20(uint64_t mb) {
    if (me_bits(mb, 1, 8) != 0x20) return false;
    if (me_bits(mb, 9, 32) == 0 && me_bits(mb, 33, 56) == 0) return true;  // empty callsign
    for (int i = 0; i < 8; i++) {
        const unsigned c = me_bits(mb, 9 + 6 * i, 14 + 6 * i);
        if (!((c >= 1 && c <= 26) || c == 32 || (c >= 48 && c <= 57))) return false;
    }
    return true;
}

// 4,0: selected vertical intention.
struct Bds40 {
    std::optional<int> mcp_alt_ft, fms_alt_ft;
    std::optional<double> baro_hpa;
};
std::optional<Bds40> parse_bds40(uint64_t mb) {
    if (wrong_status(mb, 1, 2, 13) || wrong_status(mb, 14, 15, 26) || wrong_status(mb, 27, 28, 39) ||
        wrong_status(mb, 48, 49, 51) || wrong_status(mb, 54, 55, 56) || me_bits(mb, 40, 47) || me_bits(mb, 52, 53))
        return std::nullopt;
    Bds40 r;
    if (me_bits(mb, 1, 1)) r.mcp_alt_ft = static_cast<int>(me_bits(mb, 2, 13)) * 16;
    if (me_bits(mb, 14, 14)) r.fms_alt_ft = static_cast<int>(me_bits(mb, 15, 26)) * 16;
    if (me_bits(mb, 27, 27)) r.baro_hpa = me_bits(mb, 28, 39) * 0.1 + 800;
    return r;
}

// 5,0: track and turn report.
struct Bds50 {
    std::optional<double> roll_deg, track_deg;
    std::optional<int> gs_kt, tas_kt;
};
std::optional<Bds50> parse_bds50(uint64_t mb) {
    if (wrong_status(mb, 1, 3, 11) || wrong_status(mb, 12, 13, 23) || wrong_status(mb, 24, 25, 34) ||
        wrong_status(mb, 35, 36, 45) || wrong_status(mb, 46, 47, 56))
        return std::nullopt;
    Bds50 r;
    if (me_bits(mb, 1, 1)) r.roll_deg = signed_field(mb, 2, 3, 11) * 45.0 / 256;
    if (me_bits(mb, 12, 12)) {
        const double trk = signed_field(mb, 13, 14, 23) * 90.0 / 512;
        r.track_deg = trk < 0 ? trk + 360 : trk;
    }
    if (me_bits(mb, 24, 24)) r.gs_kt = static_cast<int>(me_bits(mb, 25, 34)) * 2;
    if (me_bits(mb, 46, 46)) r.tas_kt = static_cast<int>(me_bits(mb, 47, 56)) * 2;
    if ((r.roll_deg && std::abs(*r.roll_deg) > 50) || (r.gs_kt && *r.gs_kt > 600) || (r.tas_kt && *r.tas_kt > 600) ||
        (r.gs_kt && r.tas_kt && std::abs(*r.tas_kt - *r.gs_kt) > 200))
        return std::nullopt;
    return r;
}

// 6,0: heading and speed report.
struct Bds60 {
    std::optional<double> heading_deg, mach;
    std::optional<int> ias_kt;
};
std::optional<Bds60> parse_bds60(uint64_t mb, std::optional<int> alt_ft) {
    if (wrong_status(mb, 1, 2, 12) || wrong_status(mb, 13, 14, 23) || wrong_status(mb, 24, 25, 34) ||
        wrong_status(mb, 35, 36, 45) || wrong_status(mb, 46, 47, 56))
        return std::nullopt;
    Bds60 r;
    if (me_bits(mb, 1, 1)) {
        const double hdg = signed_field(mb, 2, 3, 12) * 90.0 / 512;
        r.heading_deg = hdg < 0 ? hdg + 360 : hdg;
    }
    if (me_bits(mb, 13, 13)) r.ias_kt = static_cast<int>(me_bits(mb, 14, 23));
    if (me_bits(mb, 24, 24)) r.mach = me_bits(mb, 25, 34) * 2.048 / 512;
    // Vertical rates: 0 and 511 both mean level. Not kept, since ADS-B gives a finer one, but range checked.
    auto vr_fpm = [mb](int status, int sign) {
        const unsigned v = me_bits(mb, sign + 1, sign + 9);
        return !me_bits(mb, status, status) || v == 0 || v == 511 ? 0 : signed_field(mb, sign, sign + 1, sign + 9) * 32;
    };
    if ((r.ias_kt && *r.ias_kt > 500) || (r.mach && *r.mach > 1) || std::abs(vr_fpm(35, 36)) > 6000 ||
        std::abs(vr_fpm(46, 47)) > 6000)
        return std::nullopt;
    if (r.ias_kt && r.mach && alt_ft && std::abs(*r.ias_kt - mach_to_cas_kt(*r.mach, *alt_ft)) > 20)
        return std::nullopt;
    return r;
}

void apply_bds(AircraftState& ac, uint64_t mb, unsigned bds, std::optional<int> alt_ft, bool& changed) {
    if (bds == 0x20) {
        if (auto cs = decode_callsign(mb)) set_if_changed(ac.callsign, std::move(cs), changed);
    } else if (bds == 0x40) {
        const Bds40 r = *parse_bds40(mb);
        if (r.mcp_alt_ft || r.fms_alt_ft) {
            set_if_changed(ac.selected_altitude_ft, r.mcp_alt_ft ? r.mcp_alt_ft : r.fms_alt_ft, changed);
            set_if_changed(ac.selected_altitude_source, std::string(r.mcp_alt_ft ? "MCP" : "FMS"), changed);
        }
        if (r.baro_hpa) set_if_changed(ac.baro_setting_hpa, r.baro_hpa, changed);
    } else if (bds == 0x50) {
        // Its track and ground speed aren't taken: ADS-B's are finer, and taking both would flap.
        const Bds50 r = *parse_bds50(mb);
        if (r.roll_deg) set_if_changed(ac.roll_deg, r.roll_deg, changed);
        if (r.tas_kt) set_if_changed(ac.tas_kt, r.tas_kt, changed);
    } else if (bds == 0x60) {
        const Bds60 r = *parse_bds60(mb, alt_ft);
        if (r.heading_deg) set_if_changed(ac.heading_deg, r.heading_deg, changed);
        if (r.ias_kt) set_if_changed(ac.ias_kt, r.ias_kt, changed);
        if (r.mach) set_if_changed(ac.mach, r.mach, changed);
    }
}

}  // namespace

unsigned infer_bds(uint64_t mb, const AircraftState& ref, std::optional<int> alt_ft) {
    if (mb == 0) return 0;
    const bool other = is_bds10(mb) || is_bds17(mb) || is_bds30(mb);
    const bool is20 = is_bds20(mb);
    const auto b40 = parse_bds40(mb);
    const auto b50 = parse_bds50(mb);
    const auto b60 = parse_bds60(mb, alt_ft);
    const int n = other + is20 + b40.has_value() + b50.has_value() + b60.has_value();
    if (n == 1) return is20 ? 0x20 : b40 ? 0x40 : b50 ? 0x50 : b60 ? 0x60 : 0;
    // 5,0 and 6,0 are the usual pair to fit the same field. The right one agrees with the ADS-B velocity:
    // 5,0's track and speed closely, 6,0's magnetic heading to within variation and wind correction.
    if (n != 2 || !b50 || !b60 || !ref.ground_speed_kt || !ref.track_deg) return 0;
    const bool fits50 = b50->gs_kt && b50->track_deg && std::abs(*b50->gs_kt - *ref.ground_speed_kt) <= 30 &&
                        angle_diff(*b50->track_deg, *ref.track_deg) <= 20;
    const bool fits60 = b60->heading_deg && angle_diff(*b60->heading_deg, *ref.track_deg) <= 45;
    if (fits50 == fits60) return 0;
    return fits50 ? 0x50 : 0x60;
}

bool update_aircraft_from_me(AircraftState& ac, unsigned __int128 payload, double t) {
    uint64_t me56 = me_field(payload);
    unsigned tc = static_cast<unsigned>((me56 >> 51) & 0x1F);
    bool changed = false;
    if (tc >= 1 && tc <= 4) {
        auto cs = decode_callsign(me56);
        if (cs && ac.callsign != cs) {
            ac.callsign = cs;
            changed = true;
        }
        unsigned ca = (me56 >> 48) & 0x7;
        if (ca != 0)
            set_if_changed(ac.category, std::string{static_cast<char>('A' + (4 - tc)), static_cast<char>('0' + ca)},
                           changed);
    } else if (tc >= 5 && tc <= 8) {
        set_if_changed(ac.on_ground, true, changed);
    } else if (tc >= 9 && tc <= 18) {
        set_if_changed(ac.on_ground, false, changed);
        handle_position(ac, me56, t, changed);
    } else if (tc == 19) {
        handle_velocity(ac, me56, changed);
    } else if (tc >= 20 && tc <= 22) {
        set_if_changed(ac.on_ground, false, changed);
    } else if (tc == 28 && ((me56 >> 48) & 0x7) == 1) {
        set_if_changed(ac.squawk, decode_squawk((me56 >> 32) & 0x1FFF), changed);
    } else if (tc == 29) {
        handle_target_state(ac, me56, changed);
    } else if (tc == 31) {
        handle_operational_status(ac, me56, changed);
    }
    return changed;
}

bool update_aircraft_from_frame(AircraftState& ac, unsigned df, unsigned __int128 payload, double t) {
    bool changed = false;
    // Bits 6-8 are the flight status in DF4/5/20/21, and the capability in DF11. DF17 has a capability too,
    // but its type codes say more, and taking both would flip on_ground whenever they disagree.
    if (df == 4 || df == 5 || df == 20 || df == 21) apply_flight_status(ac, frame_bits(payload, 6, 8), changed);
    if (df == 11) {
        const unsigned ca = frame_bits(payload, 6, 8);
        if (ca == 4 || ca == 5) set_if_changed(ac.on_ground, ca == 4, changed);  // 6 and 7 are either
    }
    if (df == 0 || df == 16) set_if_changed(ac.on_ground, frame_bits(payload, 6, 6) == 1, changed);  // VS
    std::optional<int> alt;
    if (df == 0 || df == 4 || df == 16 || df == 20) {
        alt = decode_alt13(frame_bits(payload, 20, 32));
        if (alt) set_if_changed(ac.altitude_ft, alt, changed);
    }
    if (df == 5 || df == 21) set_if_changed(ac.squawk, decode_squawk(frame_bits(payload, 20, 32)), changed);
    if (df == 20 || df == 21) {
        // `ac` is the reference for infer_bds(): Comm-B never sets its ground speed or track.
        const uint64_t mb = me_field(payload);
        apply_bds(ac, mb, infer_bds(mb, ac, alt), alt, changed);
    }
    if (df == 17 || df == 18) changed |= update_aircraft_from_me(ac, payload, t);
    return changed;
}

namespace {

std::string format(const char* fmt, auto... args) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt, args...);
    return buf;
}

// The ME field of a DF17/18 frame, described from `s`, the state decoded out of that frame alone.
void describe_me(const AircraftState& s, uint64_t me56, std::vector<std::string>& parts) {
    const unsigned tc = me_bits(me56, 1, 5);
    const unsigned st = me_bits(me56, 6, 8);
    if (tc >= 1 && tc <= 4) {
        parts.emplace_back("Ident");
        if (s.callsign) parts.push_back(*s.callsign);
        if (s.category) parts.push_back(*s.category);
    } else if (tc >= 5 && tc <= 8) {
        parts.emplace_back("Surface position");
    } else if (tc >= 9 && tc <= 18) {
        parts.emplace_back("Position");
        if (s.altitude_ft) parts.push_back(format("%d ft", *s.altitude_ft));
        if (s.alert) parts.emplace_back("alert");
        if (s.spi) parts.emplace_back("SPI");
    } else if (tc == 19) {
        parts.emplace_back("Velocity");
        if (s.ground_speed_kt) parts.push_back(format("%.0f kt GS", *s.ground_speed_kt));
        if (s.track_deg) parts.push_back(format("trk %.0f°", *s.track_deg));
        if (s.heading_deg) parts.push_back(format("hdg %.0f°", *s.heading_deg));
        if (s.ias_kt) parts.push_back(format("%d kt IAS", *s.ias_kt));
        if (s.tas_kt) parts.push_back(format("%d kt TAS", *s.tas_kt));
        if (s.vertical_rate_fpm) parts.push_back(*s.vertical_rate_fpm == 0 ? "level" : format("%+d fpm", *s.vertical_rate_fpm));
    } else if (tc >= 20 && tc <= 22) {
        parts.emplace_back("Position (GNSS height)");
    } else if (tc == 23) {
        parts.emplace_back("Test");
    } else if (tc == 24) {
        parts.emplace_back("Surface system status");
    } else if (tc == 28 && st == 1) {
        static const char* const kEmergencies[] = {"",           "emergency",   "medical", "minimum fuel",
                                                   "no comms",   "hijack",      "downed",  "reserved"};
        parts.emplace_back("Emergency status");
        if (s.squawk) parts.push_back("squawk " + *s.squawk);
        if (const unsigned e = me_bits(me56, 9, 11)) parts.emplace_back(kEmergencies[e]);
    } else if (tc == 28 && st == 2) {
        parts.emplace_back("TCAS RA broadcast");
    } else if (tc == 29) {
        parts.emplace_back(me_bits(me56, 6, 7) == 1 ? "Target state" : "Target state (v1)");
        if (s.selected_altitude_ft) parts.push_back(format("sel %d ft", *s.selected_altitude_ft));
        if (s.selected_heading_deg) parts.push_back(format("sel hdg %.0f°", *s.selected_heading_deg));
        if (s.autopilot_modes)
            for (const auto& m : *s.autopilot_modes) parts.push_back(m);
    } else if (tc == 31) {
        parts.emplace_back(st == 1 ? "Operational status (surface)" : "Operational status");
        if (s.adsb_version) parts.push_back(format("ADS-B v%d", *s.adsb_version));
        if (s.nac_p) parts.push_back(format("NACp %d", *s.nac_p));
        if (s.sil) parts.push_back(format("SIL %d", *s.sil));
    } else {
        parts.push_back(tc == 0 ? std::string("No position") : format("Reserved TC%u", tc));
    }
}

// The Comm-B register of a DF20/21 reply, described from `s` as for describe_me().
void describe_bds(const AircraftState& s, unsigned bds, std::vector<std::string>& parts) {
    if (bds == 0) return;
    parts.push_back(format("BDS %X,%X", bds >> 4, bds & 0xF));
    if (bds == 0x20 && s.callsign) parts.push_back(*s.callsign);
    if (bds == 0x40 && s.selected_altitude_ft) parts.push_back(format("sel %d ft", *s.selected_altitude_ft));
    if (bds == 0x40 && s.baro_setting_hpa) parts.push_back(format("%.1f hPa", *s.baro_setting_hpa));
    if (bds == 0x50 && s.roll_deg) parts.push_back(format("roll %.1f°", *s.roll_deg));
    if (bds == 0x50 && s.tas_kt) parts.push_back(format("%d kt TAS", *s.tas_kt));
    if (bds == 0x60 && s.heading_deg) parts.push_back(format("hdg %.0f°", *s.heading_deg));
    if (bds == 0x60 && s.ias_kt) parts.push_back(format("%d kt IAS", *s.ias_kt));
    if (bds == 0x60 && s.mach) parts.push_back(format("M%.3f", *s.mach));
}

}  // namespace

std::string frame_summary(const DecodedFrameView& v, const AircraftState* ref) {
    if (v.crc_checked && !v.crc_ok) return {};
    // Only what this frame says, but with the aircraft's ADS-B velocity, which infer_bds() may need.
    AircraftState s;
    if (ref) {
        s.ground_speed_kt = ref->ground_speed_kt;
        s.track_deg = ref->track_deg;
    }
    unsigned bds = 0;
    if (v.df == 20 || v.df == 21)
        bds = infer_bds(me_field(v.payload), s, v.df == 20 ? decode_alt13(frame_bits(v.payload, 20, 32)) : std::nullopt);
    update_aircraft_from_frame(s, v.df, v.payload, 0);
    std::vector<std::string> parts;
    auto add_reply_fields = [&] {
        if (s.altitude_ft) parts.push_back(format("%d ft", *s.altitude_ft));
        if (s.squawk) parts.push_back("squawk " + *s.squawk);
        if (s.on_ground == true) parts.emplace_back("ground");
        if (s.alert) parts.emplace_back("alert");
        if (s.spi) parts.emplace_back("SPI");
    };
    switch (v.df) {
    case 0: parts.emplace_back("ACAS"); add_reply_fields(); break;
    case 4: parts.emplace_back("Altitude"); add_reply_fields(); break;
    case 5: parts.emplace_back("Identity"); add_reply_fields(); break;
    case 11: parts.emplace_back("All-call"); add_reply_fields(); break;
    case 16: parts.emplace_back("ACAS long"); add_reply_fields(); break;
    case 17: case 18: describe_me(s, me_field(v.payload), parts); break;
    case 19: parts.emplace_back("Military ES"); break;
    case 20: parts.emplace_back("Comm-B altitude"); add_reply_fields(); describe_bds(s, bds, parts); break;
    case 21: parts.emplace_back("Comm-B identity"); add_reply_fields(); describe_bds(s, bds, parts); break;
    case 22: parts.emplace_back("Military"); break;
    default: if (v.df >= 24) parts.emplace_back("Comm-D (ELM)"); break;
    }
    std::string out;
    for (const auto& part : parts) out += (out.empty() ? "" : " · ") + part;
    return out;
}

void update_aircraft_reception(AircraftState& ac, float confidence, unsigned num_bits) {
    // About the last 10 frames.
    constexpr double kAlpha = 0.1;
    const double db = 20 * std::log10(std::max(static_cast<double>(confidence) / num_bits, 1e-6));
    ac.signal_db = ac.signal_db ? *ac.signal_db + kAlpha * (db - *ac.signal_db) : db;
    ac.messages++;
}

namespace {

template <typename T>
nlohmann::json or_null(const std::optional<T>& v) {
    return v ? nlohmann::json(*v) : nlohmann::json(nullptr);
}

}  // namespace

void to_json(nlohmann::json& j, const AircraftState& ac) {
    char icao_str[7];
    std::snprintf(icao_str, sizeof(icao_str), "%06x", ac.icao);

    j["type"] = "aircraft";
    j["icao"] = icao_str;
    j["callsign"] = ac.callsign ? nlohmann::json(*ac.callsign) : nlohmann::json(nullptr);
    j["category"] = ac.category ? nlohmann::json(*ac.category) : nlohmann::json(nullptr);
    j["squawk"] = ac.squawk ? nlohmann::json(*ac.squawk) : nlohmann::json(nullptr);
    j["altitude_ft"] = ac.altitude_ft ? nlohmann::json(*ac.altitude_ft) : nlohmann::json(nullptr);
    j["lat"] = ac.lat ? nlohmann::json(*ac.lat) : nlohmann::json(nullptr);
    j["lon"] = ac.lon ? nlohmann::json(*ac.lon) : nlohmann::json(nullptr);
    j["ground_speed_kt"] = ac.ground_speed_kt ? nlohmann::json(*ac.ground_speed_kt) : nlohmann::json(nullptr);
    j["track_deg"] = ac.track_deg ? nlohmann::json(*ac.track_deg) : nlohmann::json(nullptr);
    j["vertical_rate_fpm"] = ac.vertical_rate_fpm ? nlohmann::json(*ac.vertical_rate_fpm) : nlohmann::json(nullptr);
    j["heading_deg"] = or_null(ac.heading_deg);
    j["ias_kt"] = or_null(ac.ias_kt);
    j["tas_kt"] = or_null(ac.tas_kt);
    j["mach"] = or_null(ac.mach);
    j["roll_deg"] = or_null(ac.roll_deg);
    j["on_ground"] = or_null(ac.on_ground);
    j["alert"] = ac.alert;
    j["spi"] = ac.spi;
    j["selected_altitude_ft"] = or_null(ac.selected_altitude_ft);
    j["selected_altitude_source"] = or_null(ac.selected_altitude_source);
    j["selected_heading_deg"] = or_null(ac.selected_heading_deg);
    j["baro_setting_hpa"] = or_null(ac.baro_setting_hpa);
    j["autopilot_modes"] = or_null(ac.autopilot_modes);
    j["adsb_version"] = or_null(ac.adsb_version);
    j["nac_p"] = or_null(ac.nac_p);
    j["sil"] = or_null(ac.sil);
    j["last_seen_s"] = ac.last_seen_s;
    j["last_seen_unix_s"] = ac.last_seen_unix_s;
    j["messages"] = ac.messages;
    j["signal_db"] = ac.signal_db ? nlohmann::json(*ac.signal_db) : nlohmann::json(nullptr);
}
