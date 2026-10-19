#include "api/convert.h"

#include <string_view>

namespace {

api::Aircraft::AltitudeSource altitude_source(std::string_view s) {
    if (s == "MCP") return api::Aircraft::MCP;
    if (s == "FMS") return api::Aircraft::FMS;
    return api::Aircraft::ALTITUDE_SOURCE_UNSPECIFIED;
}

api::AutopilotModes::Mode autopilot_mode(std::string_view s) {
    if (s == "AP") return api::AutopilotModes::AP;
    if (s == "VNAV") return api::AutopilotModes::VNAV;
    if (s == "ALT") return api::AutopilotModes::ALT;
    if (s == "APP") return api::AutopilotModes::APP;
    if (s == "LNAV") return api::AutopilotModes::LNAV;
    return api::AutopilotModes::MODE_UNSPECIFIED;
}

}  // namespace

api::Aircraft to_proto(const AircraftState& s) {
    api::Aircraft a;
    a.set_icao(s.icao);
    if (s.callsign) a.set_callsign(*s.callsign);
    if (s.category) a.set_category(*s.category);
    if (s.squawk) a.set_squawk(*s.squawk);
    if (s.altitude_ft) a.set_altitude_ft(*s.altitude_ft);
    if (s.lat) a.set_lat(*s.lat);
    if (s.lon) a.set_lon(*s.lon);
    if (s.ground_speed_kt) a.set_ground_speed_kt(*s.ground_speed_kt);
    if (s.track_deg) a.set_track_deg(*s.track_deg);
    if (s.vertical_rate_fpm) a.set_vertical_rate_fpm(*s.vertical_rate_fpm);
    if (s.heading_deg) a.set_heading_deg(*s.heading_deg);
    if (s.ias_kt) a.set_ias_kt(*s.ias_kt);
    if (s.tas_kt) a.set_tas_kt(*s.tas_kt);
    if (s.mach) a.set_mach(*s.mach);
    if (s.roll_deg) a.set_roll_deg(*s.roll_deg);
    if (s.on_ground) a.set_on_ground(*s.on_ground);
    a.set_alert(s.alert);
    a.set_spi(s.spi);
    if (s.selected_altitude_ft) a.set_selected_altitude_ft(*s.selected_altitude_ft);
    if (s.selected_altitude_source) a.set_selected_altitude_source(altitude_source(*s.selected_altitude_source));
    if (s.selected_heading_deg) a.set_selected_heading_deg(*s.selected_heading_deg);
    if (s.baro_setting_hpa) a.set_baro_setting_hpa(*s.baro_setting_hpa);
    if (s.autopilot_modes) {
        // Set even when empty: "none engaged" differs from "not known yet".
        api::AutopilotModes& modes = *a.mutable_autopilot_modes();
        for (const std::string& m : *s.autopilot_modes) modes.add_modes(autopilot_mode(m));
    }
    if (s.adsb_version) a.set_adsb_version(*s.adsb_version);
    if (s.nac_p) a.set_nac_p(*s.nac_p);
    if (s.sil) a.set_sil(*s.sil);
    a.set_messages(s.messages);
    if (s.signal_db) a.set_signal_db(*s.signal_db);
    a.set_last_seen_s(s.last_seen_s);
    a.set_last_seen_unix_s(s.last_seen_unix_s);
    return a;
}

api::Frame to_proto(const DecodedFrameView& v, std::string summary) {
    api::Frame f;
    f.set_idx(v.idx);
    f.set_bits(v.num_bits);
    f.set_confidence(v.confidence);
    f.set_df(v.df);
    if (v.icao_known) f.set_icao(v.icao);
    f.set_crc_ok(v.crc_ok);
    f.set_crc_checked(v.crc_checked);
    if (v.fixed_bit >= 0) f.set_crc_fixed_bit(static_cast<uint32_t>(v.fixed_bit));
    // The frame's first bit is bit 111 of `payload`, for short frames too.
    std::string& payload = *f.mutable_payload();
    payload.resize(v.num_bits / 8);
    for (size_t i = 0; i < payload.size(); i++)
        payload[i] = static_cast<char>(static_cast<uint8_t>(v.payload >> (104 - 8 * i)));
    f.set_summary(std::move(summary));
    return f;
}

api::SpectrumRow to_proto(const SpectrumFrame& s) {
    api::SpectrumRow row;
    row.set_freq_hz(s.freq_hz);
    row.set_rate_hz(s.rate_hz);
    row.mutable_bins_db()->Assign(s.mag_db.begin(), s.mag_db.end());
    return row;
}
