add_library(adsb_output STATIC ws_publisher.cpp aircraft_history.cpp frame_recorder.cpp)
target_link_libraries(adsb_output PUBLIC adsb_decode coro::coro PRIVATE SQLite::SQLite3 HighFive)
