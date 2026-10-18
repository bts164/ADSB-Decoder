# Simulated ADS-B IQ source (vita49_send's `sim` subcommand, adsb_sim_eval).
# Its noise/quantize kernel is its own ispc object so the simulator stays
# independent of the decoder's kernels.
add_library(adsb_sim STATIC adsb_sim.cpp)
# ispc's fast log/sincos: the kernel only turns hashes into Gaussian noise, where their lower accuracy is
# invisible, and it is most of the render time at high sample rates. The output stays deterministic.
adsb_add_ispc(adsb_sim sim_kernels.ispc --math-lib=fast)
target_link_libraries(adsb_sim PUBLIC adsb_common coro::coro xtensor nlohmann_json::nlohmann_json)
