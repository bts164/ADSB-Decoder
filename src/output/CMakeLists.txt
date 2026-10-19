add_library(adsb_output STATIC api_server.cpp aircraft_history.cpp frame_recorder.cpp)
target_link_libraries(adsb_output PUBLIC adsb_decode adsb_api coro::coro PRIVATE SQLite::SQLite3 HighFive)
