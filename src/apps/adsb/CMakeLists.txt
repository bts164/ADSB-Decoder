add_executable(adsb main.cpp pipeline.cpp)
target_link_libraries(adsb PRIVATE adsb_input adsb_output adsb_decode argparse::argparse)
install(TARGETS adsb RUNTIME DESTINATION bin)
