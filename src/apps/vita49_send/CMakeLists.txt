# Standalone test-source utility: rtlsdr -> VITA-49/VRT -> UDP. See
# doc/design/vita49-format.md -- independent of the adsb demod pipeline, so
# it's its own executable rather than a mode of adsb.
add_executable(vita49_send main.cpp pace_stats.cpp packet_sender.cpp rtlsdr_source.cpp sim_source.cpp)
target_link_libraries(vita49_send PRIVATE adsb_common adsb_input adsb_net adsb_sim adsb_vrt coro::coro argparse::argparse Threads::Threads)
install(TARGETS vita49_send RUNTIME DESTINATION bin)
