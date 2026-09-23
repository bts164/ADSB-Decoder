#include "pipeline.h"

#include <nlohmann/json.hpp>

#include "aircraft.h"
#include "aircraft_history.h"
#include "frame_decode.h"

void handle_frame(const AdsbFrame& f, double rate_hz, uint64_t base_sample_index, bool print_stdout) {
    DecodedFrameView v = compute_frame_view(f, rate_hz, base_sample_index);
    if (print_stdout) print_frame_stdout(v);
    ws_publisher_publish(nlohmann::json(v).dump());

    if (v.crc_ok && v.icao_known && (v.df == 17 || v.df == 18)) {
        double t_s = static_cast<double>(v.idx) / rate_hz;
        AircraftState& ac = aircraft_table()[v.icao];
        ac.icao = v.icao;
        bool changed = update_aircraft_from_me(ac, f.payload, t_s);
        ac.last_seen_s = t_s;
        if (changed) {
            if (print_stdout) print_aircraft_stdout(ac);
            ws_publisher_publish(nlohmann::json(ac).dump());
            aircraft_history_record(ac);
        }
    }
}
