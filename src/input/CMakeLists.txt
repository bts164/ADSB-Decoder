# IQ sources for `adsb file` / `adsb vita49` / `adsb rtlsdr`.
add_library(adsb_input STATIC file_stream.cpp rtlsdr_device.cpp rtlsdr_stream.cpp iq_block_chunker.cpp vita49_stream.cpp vrt_assembler.cpp sigmf_meta.cpp)
target_link_libraries(adsb_input PUBLIC adsb_dsp coro::coro librtlsdr::librtlsdr Threads::Threads)
