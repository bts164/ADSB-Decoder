# IQ sources for `adsb file` / `adsb vita49` / `adsb rtlsdr` / `adsb sidekiq` / `adsb uhd`.
add_library(adsb_input STATIC file_stream.cpp rtlsdr_device.cpp rtlsdr_stream.cpp vita49_stream.cpp sigmf_meta.cpp)
target_link_libraries(adsb_input PUBLIC adsb_dsp adsb_vrt coro::coro librtlsdr::librtlsdr Threads::Threads
                                 PRIVATE argparse::argparse)
if(ADSB_WITH_SIDEKIQ)
    target_sources(adsb_input PRIVATE sidekiq_stream.cpp)
    # SYSTEM: the SDK's headers are C (anonymous structs, flexible array members), which C++ warnings flag.
    target_include_directories(adsb_input SYSTEM PRIVATE "${SIDEKIQ_SDK_DIR}/sidekiq_core/inc")
    target_compile_definitions(adsb_input PUBLIC ADSB_WITH_SIDEKIQ)
    target_link_libraries(adsb_input PRIVATE ${CMAKE_DL_LIBS})
endif()
if(ADSB_WITH_UHD)
    target_sources(adsb_input PRIVATE uhd_stream.cpp)
    target_compile_definitions(adsb_input PUBLIC ADSB_WITH_UHD)
    # UHD's headers include Boost's.
    target_link_libraries(adsb_input PRIVATE uhd::uhd Boost::headers)
endif()
