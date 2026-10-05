# ispc launch/sync runtime (tasksys.cpp) is built on OpenMP.
add_library(adsb_common STATIC cpu_layout.cpp tasksys.cpp)
target_include_directories(adsb_common PUBLIC ${PROJECT_SOURCE_DIR}/src)
# log.h: every library logs through Abseil; the apps also initialize it (apps/log_options.h).
target_link_libraries(adsb_common PUBLIC OpenMP::OpenMP_CXX absl::log absl::log_globals absl::log_initialize
                                         absl::log_sink absl::log_sink_registry absl::str_format)
