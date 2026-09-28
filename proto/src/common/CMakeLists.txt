# ispc launch/sync runtime (tasksys.cpp) is built on OpenMP.
add_library(adsb_common STATIC cpu_layout.cpp tasksys.cpp)
target_include_directories(adsb_common PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(adsb_common PUBLIC OpenMP::OpenMP_CXX)
