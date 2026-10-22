add_library(adsb_output STATIC api_server.cpp aircraft_history.cpp capture.cpp)
target_link_libraries(adsb_output PUBLIC adsb_decode adsb_api coro::coro PRIVATE SQLite::SQLite3)
if(ADSB_WITH_HDF5)
    target_sources(adsb_output PRIVATE frame_recorder.cpp)
    target_link_libraries(adsb_output PRIVATE HighFive)
    # PUBLIC: the code that uses FrameRecorder checks it too.
    target_compile_definitions(adsb_output PUBLIC ADSB_WITH_HDF5)
endif()
