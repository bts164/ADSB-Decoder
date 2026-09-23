#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json_fwd.hpp>

// --- Aircraft state tracking (DF17/18 extended-squitter decoding) ------
//
// Decodes lat/lon (via CPR even/odd pairing), barometric altitude,
// callsign, and ground velocity out of the ME field of CRC-valid DF17/18
// frames, and keeps one running AircraftState per ICAO address in
// aircraft_table() below.
//
// Only the common subset of the ADS-B message set is handled: type codes
// 1-4 (identification), 9-18 (airborne position, barometric altitude
// only -- Gillham/Q=0 encoded altitudes are left unset), and 19 subtypes
// 1-2 (ground-speed velocity). Surface position, GNSS-height position,
// airspeed/heading velocity subtypes, and status messages are not
// decoded; this is deliberately the "basic" slice, matching the same
// scope philosophy as the frontend table (see ui/frontend/lib/main.dart).
struct AircraftState {
    uint32_t icao = 0;
    std::optional<std::string> callsign;
    std::optional<int> altitude_ft;
    std::optional<double> lat;
    std::optional<double> lon;
    std::optional<double> ground_speed_kt;
    std::optional<double> track_deg;
    std::optional<int> vertical_rate_fpm;
    double last_seen_s = 0;

    // Most recent even/odd CPR-encoded airborne position reports, held
    // until a same-parity-mismatched pair arrives close enough in time to
    // resolve into an unambiguous lat/lon (global CPR decode).
    struct CprSlot {
        uint32_t lat_cpr = 0;
        uint32_t lon_cpr = 0;
        double t = 0;
        bool have = false;
    };
    CprSlot cpr_even;
    CprSlot cpr_odd;
};

// Single running table keyed by ICAO address, built up as frames arrive --
// same function-local-static pattern (and same "only one writer thread per
// run" justification for the lack of locking) as frame_decode.cpp's
// known_icaos(). Only ever called from pipeline.cpp's handle_frame.
std::unordered_map<uint32_t, AircraftState>& aircraft_table();

// Dispatches on the ME field's type code (top 5 bits) and updates `ac` in
// place. Returns true if any displayed field of `ac` changed, so the caller
// knows whether to publish an update (aircraft state is only published on
// change, not per-frame).
bool update_aircraft_from_me(AircraftState& ac, unsigned __int128 payload, double t);

// nlohmann::json ADL customization point -- see frame_decode.h's to_json for
// why this replaces a named aircraft_to_json function.
void to_json(nlohmann::json& j, const AircraftState& ac);

void print_aircraft_stdout(const AircraftState& ac);
