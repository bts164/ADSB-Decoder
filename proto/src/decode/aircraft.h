#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "decode/frame_decode.h"

/**
 * @file
 * Aircraft state tracking from Mode S replies and DF17/18 extended squitters.
 *
 * Out of the ME field of DF17/18: type codes 1-4 (callsign and category), 5-8 (surface position, for the
 * on-ground flag only), 9-18 (airborne position with barometric altitude and surveillance status), 19
 * (ground speed, track and vertical rate, or magnetic heading and airspeed), 20-22 (GNSS-height position,
 * for the airborne flag only), 28 subtype 1 (emergency status, for its squawk) and 29 subtype 1 (target
 * state: selected altitude and heading, baro setting, autopilot modes) and 31 (operational status: ADS-B
 * version, NACp, SIL). Out of the other replies: altitude from DF0/4/16/20, squawk from DF5/21, flight
 * status (on ground, alert, SPI) from DF4/5/20/21, vertical status from DF0/16, capability (on ground) from
 * DF11, and the Comm-B registers BDS 2,0, 4,0, 5,0 and 6,0 from the MB field of DF20/21 (see infer_bds()).
 * Keeps one running AircraftState per ICAO address in aircraft_table().
 *
 * Not decoded: surface and GNSS positions, other Comm-B registers, ACAS advisories, and Gillham (Q=0) or
 * metric altitudes, which are left unset.
 */

/** Latest known state of one aircraft. Every decoded field is empty until a message carrying it arrives. */
struct AircraftState {
    uint32_t icao = 0;
    std::optional<std::string> callsign;
    /**
     * ADS-B emitter category as set letter + number, e.g. "A3" (large aircraft): set A from type code 4, B
     * from 3, C from 2, D from 1. Unset when the aircraft reports category 0 (no information).
     */
    std::optional<std::string> category;
    /** Mode A code as four octal digits, e.g. "7700". */
    std::optional<std::string> squawk;
    std::optional<int> altitude_ft;
    std::optional<double> lat;  /**< degrees */
    std::optional<double> lon;  /**< degrees */
    std::optional<double> ground_speed_kt;
    std::optional<double> track_deg;  /**< clockwise from north */
    std::optional<int> vertical_rate_fpm;
    std::optional<double> heading_deg;  /**< magnetic heading, from TC19 subtypes 3-4 or BDS 6,0 */
    std::optional<int> ias_kt;          /**< indicated airspeed, from TC19 subtypes 3-4 or BDS 6,0 */
    std::optional<int> tas_kt;          /**< true airspeed, from TC19 subtypes 3-4 or BDS 5,0 */
    std::optional<double> mach;         /**< from BDS 6,0 */
    std::optional<double> roll_deg;     /**< positive is right wing down; from BDS 5,0 */
    std::optional<bool> on_ground;
    bool alert = false;  /**< flight status or surveillance status alert: squawk changed, or an emergency */
    bool spi = false;    /**< special position identification ("ident" pressed) */
    /** Target state (TC29) or BDS 4,0: the altitude the autopilot or pilot has selected. */
    std::optional<int> selected_altitude_ft;
    std::optional<std::string> selected_altitude_source;  /**< "MCP" (mode control panel) or "FMS" */
    std::optional<double> selected_heading_deg;
    std::optional<double> baro_setting_hpa;
    /** Engaged autopilot modes, of "AP", "VNAV", "ALT", "APP" and "LNAV"; empty if none, unset if unknown. */
    std::optional<std::vector<std::string>> autopilot_modes;
    /** Operational status (TC31): ADS-B version (0-2), position accuracy category and integrity level. */
    std::optional<int> adsb_version;
    std::optional<int> nac_p;  /**< navigation accuracy category for position, 0 (unknown) to 11 (< 3 m) */
    std::optional<int> sil;    /**< source integrity level, 0 (unknown) to 3 (<= 1e-7 per hour or sample) */
    double last_seen_s = 0;  /**< stream time (sample index / rate) of the last frame from this aircraft */
    /** Wall-clock time of the same frame, in Unix seconds: the stream's start time plus last_seen_s. */
    double last_seen_unix_s = 0;
    /** Frames received from this aircraft, of any DF, CRC-valid or address-matched. */
    uint64_t messages = 0;
    /**
     * Moving average of those frames' signal level in dB: the mean per-bit pulse amplitude (on-chip minus
     * off-chip envelope, AdsbFrame::confidence / num_bits) relative to its block's RMS. Relative, not
     * absolute power; unset until the first frame.
     */
    std::optional<double> signal_db;
    /** Stream time this aircraft was last published, for rate-limiting updates that only change counters. */
    double last_published_s = -1e9;

    /**
     * One CPR-encoded airborne position report, held until a report of the other parity arrives close
     * enough in time to resolve an unambiguous lat/lon.
     */
    struct CprSlot {
        uint32_t lat_cpr = 0;
        uint32_t lon_cpr = 0;
        double t = 0;
        bool have = false;
    };
    CprSlot cpr_even;
    CprSlot cpr_odd;
};

/**
 * The table of every aircraft seen so far, keyed by ICAO address. Not thread-safe; only the demod
 * thread's handle_frame() (pipeline.cpp) uses it.
 */
std::unordered_map<uint32_t, AircraftState>& aircraft_table();

/**
 * Decodes the ME field of a CRC-valid DF17/18 frame, dispatching on its type code, into `ac`.
 * @param ac aircraft to update in place
 * @param payload the 112-bit frame
 * @param t stream time of the frame, in seconds
 * @return true if any displayed field of `ac` changed, i.e. whether an update should be published
 */
bool update_aircraft_from_me(AircraftState& ac, unsigned __int128 payload, double t);

/**
 * Decodes whatever a CRC-valid frame of any DF carries about its aircraft into `ac`: the ME field of DF17/18
 * (update_aircraft_from_me) and the header fields of the other replies.
 * @param ac aircraft to update in place
 * @param df the frame's downlink format
 * @param payload the frame, first bit in bit 111 (a 56-bit reply in the leading 56 bits)
 * @param t stream time of the frame, in seconds
 * @return true if any displayed field of `ac` changed
 */
bool update_aircraft_from_frame(AircraftState& ac, unsigned df, unsigned __int128 payload, double t);

/**
 * The Comm-B register a DF20/21 MB field holds, of those decoded. The MB field doesn't say which register
 * it holds (the interrogation that asked for it did), so this checks the field against each register's
 * rules, the same checks as pyModeS: status bits that are clear must have their field zero, reserved bits
 * must be zero, and values must be in range. A field that fits both 5,0 and 6,0 is resolved by whichever
 * is closer to `ref`'s ADS-B ground speed and track; without those, or if it fits some other pair, it's
 * left undecoded (0).
 * @param mb the MB field, bits 33-88 of the frame
 * @param ref the aircraft as known so far, for resolving 5,0 against 6,0
 * @param alt_ft the reply's altitude (DF20), for checking 6,0's IAS against its Mach
 * @return 0x20, 0x40, 0x50, 0x60 or 0 if none
 */
unsigned infer_bds(uint64_t mb, const AircraftState& ref, std::optional<int> alt_ft);

/**
 * One-line summary of what a frame says, e.g. "Position · 7200 ft" or "Identity · squawk 0356 · alert", for
 * the UI's frame log. Empty for a frame that failed CRC, since its fields can't be trusted.
 * @param ref the aircraft as known before this frame, if any, for infer_bds()
 */
std::string frame_summary(const DecodedFrameView& v, const AircraftState* ref = nullptr);

/**
 * Folds one frame's signal level into `ac.signal_db` and counts it in `ac.messages`. These change with
 * every frame, so they don't count as a change to publish.
 * @param confidence the frame's AdsbFrame::confidence
 * @param num_bits bits `confidence` sums over
 */
void update_aircraft_reception(AircraftState& ac, float confidence, unsigned num_bits);

/** nlohmann::json conversion (found by ADL): `{"type":"aircraft", ...}`. */
void to_json(nlohmann::json& j, const AircraftState& ac);

/** Prints one `aircraft icao=... callsign=... ...` line to stdout. */
void print_aircraft_stdout(const AircraftState& ac);
