#include "aircraft.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <nlohmann/json.hpp>

#include "constants.h"

std::unordered_map<uint32_t, AircraftState>& aircraft_table() {
    static std::unordered_map<uint32_t, AircraftState> table;
    return table;
}

namespace {

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

void handle_position(AircraftState& ac, uint64_t me56, double t, bool& changed) {
    unsigned alt_field = (me56 >> 36) & 0xFFF;
    unsigned f_bit = (me56 >> 34) & 0x1;  // 0=even, 1=odd
    uint32_t lat_cpr = (me56 >> 17) & 0x1FFFF;
    uint32_t lon_cpr = me56 & 0x1FFFF;

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

// Ground-speed velocity (TC 19, subtypes 1-2 only; airspeed/heading
// subtypes 3-4 are not decoded).
void handle_velocity(AircraftState& ac, uint64_t me56, bool& changed) {
    unsigned st = (me56 >> 48) & 0x7;
    if (st != 1 && st != 2) return;

    unsigned ew_sign = (me56 >> 42) & 0x1;
    unsigned ew_raw = (me56 >> 32) & 0x3FF;
    unsigned ns_sign = (me56 >> 31) & 0x1;
    unsigned ns_raw = (me56 >> 21) & 0x3FF;
    unsigned vr_sign = (me56 >> 19) & 0x1;
    unsigned vr_raw = (me56 >> 10) & 0x1FF;

    if (ew_raw != 0 && ns_raw != 0) {
        int vx = static_cast<int>(ew_raw - 1) * (ew_sign ? -1 : 1);
        int vy = static_cast<int>(ns_raw - 1) * (ns_sign ? -1 : 1);
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
    if (vr_raw != 0) {
        int vr = static_cast<int>(vr_raw - 1) * 64 * (vr_sign ? -1 : 1);
        if (ac.vertical_rate_fpm != vr) {
            ac.vertical_rate_fpm = vr;
            changed = true;
        }
    }
}

}  // namespace

bool update_aircraft_from_me(AircraftState& ac, unsigned __int128 payload, double t) {
    uint64_t me56 = static_cast<uint64_t>((payload >> 24) & ((static_cast<unsigned __int128>(1) << 56) - 1));
    unsigned tc = static_cast<unsigned>((me56 >> 51) & 0x1F);
    bool changed = false;
    if (tc >= 1 && tc <= 4) {
        auto cs = decode_callsign(me56);
        if (cs && ac.callsign != cs) {
            ac.callsign = cs;
            changed = true;
        }
    } else if (tc >= 9 && tc <= 18) {
        handle_position(ac, me56, t, changed);
    } else if (tc == 19) {
        handle_velocity(ac, me56, changed);
    }
    return changed;
}

void to_json(nlohmann::json& j, const AircraftState& ac) {
    char icao_str[7];
    std::snprintf(icao_str, sizeof(icao_str), "%06x", ac.icao);

    j["type"] = "aircraft";
    j["icao"] = icao_str;
    j["callsign"] = ac.callsign ? nlohmann::json(*ac.callsign) : nlohmann::json(nullptr);
    j["altitude_ft"] = ac.altitude_ft ? nlohmann::json(*ac.altitude_ft) : nlohmann::json(nullptr);
    j["lat"] = ac.lat ? nlohmann::json(*ac.lat) : nlohmann::json(nullptr);
    j["lon"] = ac.lon ? nlohmann::json(*ac.lon) : nlohmann::json(nullptr);
    j["ground_speed_kt"] = ac.ground_speed_kt ? nlohmann::json(*ac.ground_speed_kt) : nlohmann::json(nullptr);
    j["track_deg"] = ac.track_deg ? nlohmann::json(*ac.track_deg) : nlohmann::json(nullptr);
    j["vertical_rate_fpm"] = ac.vertical_rate_fpm ? nlohmann::json(*ac.vertical_rate_fpm) : nlohmann::json(nullptr);
    j["last_seen_s"] = ac.last_seen_s;
}

void print_aircraft_stdout(const AircraftState& ac) {
    char icao_str[7];
    std::snprintf(icao_str, sizeof(icao_str), "%06x", ac.icao);
    std::string line = std::string("aircraft icao=") + icao_str;
    line += " callsign=" + (ac.callsign ? *ac.callsign : std::string("?"));
    char buf[64];
    if (ac.altitude_ft) line += " alt=" + std::to_string(*ac.altitude_ft) + "ft";
    if (ac.lat && ac.lon) {
        std::snprintf(buf, sizeof(buf), " lat=%.5f lon=%.5f", *ac.lat, *ac.lon);
        line += buf;
    }
    if (ac.ground_speed_kt) {
        std::snprintf(buf, sizeof(buf), " gs=%.0fkt", *ac.ground_speed_kt);
        line += buf;
    }
    if (ac.track_deg) {
        std::snprintf(buf, sizeof(buf), " trk=%.0f", *ac.track_deg);
        line += buf;
    }
    if (ac.vertical_rate_fpm) line += " vr=" + std::to_string(*ac.vertical_rate_fpm) + "fpm";
    std::snprintf(buf, sizeof(buf), " t=%.1fs", ac.last_seen_s);
    line += buf;
    std::printf("%s\n", line.c_str());
}
