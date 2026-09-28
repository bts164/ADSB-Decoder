#include "apps/adsb/pipeline.h"

#include <nlohmann/json.hpp>

#include "decode/aircraft.h"
#include "output/aircraft_history.h"

namespace {

// Longest an aircraft goes unpublished while frames from it keep arriving.
constexpr double kAircraftRefreshS = 1.0;

}  // namespace

DecodedFrameView handle_frame(const AdsbFrame& f, double rate_hz, double start_unix_s, bool print_stdout) {
    DecodedFrameView v = compute_frame_view(f, rate_hz);
    if (print_stdout) print_frame_stdout(v);
    nlohmann::json frame = v;
    const auto& table = aircraft_table();
    const auto known = v.icao_known ? table.find(v.icao) : table.end();
    const std::string summary = frame_summary(v, known == table.end() ? nullptr : &known->second);
    frame["summary"] = summary.empty() ? nlohmann::json(nullptr) : nlohmann::json(summary);
    ws_publisher_publish(frame.dump());

    // Every frame whose address checks out counts toward its aircraft's
    // messages and signal level, and marks it seen.
    if (v.crc_ok && v.icao_known) {
        double t_s = static_cast<double>(v.idx) / rate_hz;
        AircraftState& ac = aircraft_table()[v.icao];
        ac.icao = v.icao;
        const bool changed = update_aircraft_from_frame(ac, v.df, v.payload, t_s);
        update_aircraft_reception(ac, v.confidence, v.num_bits);
        ac.last_seen_s = t_s;
        ac.last_seen_unix_s = start_unix_s + t_s;
        // The counters and last-seen time change with every frame, so they
        // go out with the next real change or at most once a second.
        if (changed && print_stdout) print_aircraft_stdout(ac);
        if (changed || t_s - ac.last_published_s >= kAircraftRefreshS) {
            ac.last_published_s = t_s;
            std::string json = nlohmann::json(ac).dump();
            aircraft_history_record(ac, json);
            ws_publisher_publish(std::move(json));
        }
    }
    return v;
}
