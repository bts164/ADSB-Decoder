# ispc launch/sync runtime (tasksys.cpp) is built on OpenMP.
add_library(adsb_common STATIC cpu_layout.cpp tasksys.cpp)
target_include_directories(adsb_common PUBLIC ${PROJECT_SOURCE_DIR}/src)
# log.h: every library logs through Abseil; the apps also initialize it (apps/log_options.h).
target_link_libraries(adsb_common PUBLIC OpenMP::OpenMP_CXX absl::log absl::log_globals absl::log_initialize
                                         absl::log_sink absl::log_sink_registry absl::str_format)
# Present when coro was built with tracing=tracy: find_package(coro) brings it in. tasksys.cpp traces its
# launches when it is; this library doesn't link coro, so it has to ask for Tracy itself.
if(TARGET Tracy::TracyClient)
    target_link_libraries(adsb_common PUBLIC Tracy::TracyClient)
endif()

# --version and the apps' first log line (version_info.h). Only the apps link it, as its git half changes with every
# commit. That half is written by version_info_git.cmake on every build; the Conan half by conanfile.py's generate().
if(NOT ADSB_CONAN_VERSION_INFO)
    message(FATAL_ERROR "ADSB_CONAN_VERSION_INFO isn't set: configure with a preset `conan install` wrote")
endif()
find_package(Git QUIET)
set(git_cpp ${CMAKE_CURRENT_BINARY_DIR}/version_info_git.cpp)
add_custom_target(adsb_version_info_git
    COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" "-DGIT=${GIT_EXECUTABLE}"
            "-DEXPORTED=${ADSB_GIT_DESCRIBE_EXPORTED}" "-DTEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/version_info_git.cpp.in"
            "-DOUT=${git_cpp}" -P ${CMAKE_CURRENT_SOURCE_DIR}/version_info_git.cmake
    BYPRODUCTS ${git_cpp}
    COMMENT "version info: git describe")
add_library(adsb_version_info STATIC version_info.cpp ${git_cpp} ${ADSB_CONAN_VERSION_INFO})
add_dependencies(adsb_version_info adsb_version_info_git)
target_include_directories(adsb_version_info PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(adsb_version_info PRIVATE nlohmann_json::nlohmann_json)
