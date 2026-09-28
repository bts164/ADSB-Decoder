# adsb_sim (vita49_send's `sim` subcommand) tests. Links the real decoder-side
# aircraft.cpp so encoded messages are checked against the decoder itself.
add_executable(adsb_sim_test adsb_sim_test.cpp)
target_link_libraries(adsb_sim_test PRIVATE adsb_sim adsb_decode)
target_compile_definitions(adsb_sim_test PRIVATE ADSB_SIM_SCENARIO_DIR="${PROJECT_SOURCE_DIR}/scenarios")
add_test(NAME adsb_sim_test COMMAND adsb_sim_test)

# vrt_assembler (the reorder/gap-fill core of `adsb vita49` input) tests.
add_executable(vita49_stream_test vita49_stream_test.cpp)
target_link_libraries(vita49_stream_test PRIVATE adsb_input)
add_test(NAME vita49_stream_test COMMAND vita49_stream_test)

# select_preamble_peaks (the demod's candidate selection) tests.
add_executable(peak_select_test peak_select_test.cpp)
target_link_libraries(peak_select_test PRIVATE adsb_dsp)
add_test(NAME peak_select_test COMMAND peak_select_test)

# BlockStitcher (the demod's cross-block guard padding) tests.
add_executable(block_stitcher_test block_stitcher_test.cpp)
target_link_libraries(block_stitcher_test PRIVATE adsb_dsp)
add_test(NAME block_stitcher_test COMMAND block_stitcher_test)
