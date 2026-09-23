#pragma once

#include <string>
#include <vector>

#include "aircraft.h"

// Persists aircraft position history to a SQLite file, keyed by ICAO
// address, written through live as aircraft state changes (see
// pipeline.cpp's handle_frame). Same init-once/no-op-until-initialized
// pattern as ws_publisher.h -- aircraft_history_record() and
// aircraft_history_backfill_messages() are safe no-ops until
// aircraft_history_init() has been called.
void aircraft_history_init(const std::string& db_path);

// Appends one position sample for `ac` if it has a resolved lat/lon (no-op
// otherwise, since a bare position-less update -- e.g. callsign only --
// isn't a track point).
void aircraft_history_record(const AircraftState& ac);

// Returns one JSON `{"type":"history","icao":...,"track":[...]}` message
// per known aircraft that has at least one stored position, oldest point
// first, for backfilling a newly-connected WS client (see
// ws_publisher.cpp's handle_client).
std::vector<std::string> aircraft_history_backfill_messages();
