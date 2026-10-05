add_library(adsb_decode STATIC frame_decode.cpp aircraft.cpp)
target_link_libraries(adsb_decode PUBLIC adsb_dsp nlohmann_json::nlohmann_json)
