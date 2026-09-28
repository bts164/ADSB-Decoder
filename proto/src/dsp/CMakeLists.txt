add_library(adsb_dsp STATIC demod.cpp iq_block.cpp peak_select.cpp filter_bank.cpp spectrum.cpp)
adsb_add_ispc(adsb_dsp kernels.ispc)
target_compile_definitions(adsb_dsp PRIVATE ADSB_SAMPLES_PER_SYMBOL=${ADSB_SAMPLES_PER_SYMBOL})
target_link_libraries(adsb_dsp PUBLIC adsb_common xtensor nlohmann_json::nlohmann_json PRIVATE fftw::fftw)
