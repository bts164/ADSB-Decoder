# Simulated ADS-B IQ source (vita49_send's `sim` subcommand, adsb_sim_eval).
# Its noise/quantize kernel is its own ispc object so the simulator stays
# independent of the decoder's kernels.
add_library(adsb_sim STATIC adsb_sim.cpp)
adsb_add_ispc(adsb_sim sim_kernels.ispc)
target_link_libraries(adsb_sim PUBLIC adsb_common xtensor nlohmann_json::nlohmann_json)
