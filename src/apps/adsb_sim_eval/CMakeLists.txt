# Scores the demodulator against the simulator's ground truth (detection
# rate, false hits, CRC, exact bytes) -- see main.cpp.
add_executable(adsb_sim_eval main.cpp)
target_link_libraries(adsb_sim_eval PRIVATE adsb_sim adsb_decode argparse::argparse)
