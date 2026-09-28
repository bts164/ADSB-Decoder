# Standalone test-source utility: rtlsdr -> VITA-49/VRT -> UDP. See
# doc/design/vita49-format.md -- independent of the adsb demod pipeline, so
# it's its own executable rather than a mode of adsb.
add_executable(vita49_send main.cpp)
target_link_libraries(vita49_send PRIVATE adsb_sim coro::coro librtlsdr::librtlsdr argparse::argparse Threads::Threads)
install(TARGETS vita49_send RUNTIME DESTINATION bin)
