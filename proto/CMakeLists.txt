cmake_minimum_required(VERSION 3.20)
project(adsb CXX)

set(CMAKE_CXX_STANDARD 23)  # coro requires C++23 (std::expected, etc.)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compile-time constant consumed by both the ispc kernels' gang width and
# the C++ demod state machine — see README.md "Design principle:
# parameterize everything" for why this stays compile-time rather than a
# runtime config field.
set(ADSB_SAMPLES_PER_SYMBOL 12 CACHE STRING "Mode S samples per symbol (compile-time)")

# Provided by conanfile.py (see README's build instructions: `conan install
# . --build=missing` then `cmake --preset conan-release`) -- CMakeToolchain
# points find_package() at conan's cache via CMAKE_PREFIX_PATH, and CMakeDeps
# generates this config file.
find_package(coro REQUIRED)
find_package(librtlsdr REQUIRED)
find_package(argparse REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(FFTW3 REQUIRED)
find_package(SQLite3 REQUIRED)

# ISPC_ROOT (e.g. via a project-local .envrc/direnv) points this at a
# manually-extracted ispc release instead of a system package -- the ispc
# *snap* specifically can't be used here: its strict AppArmor confinement
# denies reading any dot-directory under $HOME, which includes conan's
# ~/.conan2 cache that `conan create` builds from. Falls back to PATH (and
# find_program's other default search locations) if ISPC_ROOT is unset.
find_program(ISPC_EXECUTABLE ispc HINTS "$ENV{ISPC_ROOT}/bin" REQUIRED)

# Left empty, ispc autodetects the host CPU's best ISA (e.g. avx512icl-x16
# on a machine that has it) -- see README.md's Phase-0 status section.
# Override to force a specific ISA/gang-width, e.g.
# -DADSB_ISPC_TARGET=avx2-i32x8, for comparing against autodetected AVX-512:
# AVX-512's higher power draw and clock-speed throttling can make it net
# *slower* than AVX2 on some (especially lower-end) CPUs for some workloads,
# so this is here to let that be measured rather than assumed.
set(ADSB_ISPC_TARGET "" CACHE STRING "ispc --target override (empty = autodetect host ISA)")

set(ISPC_SRC ${CMAKE_CURRENT_SOURCE_DIR}/ispc/kernels.ispc)
set(ISPC_OBJ ${CMAKE_CURRENT_BINARY_DIR}/kernels_ispc.o)
set(ISPC_HEADER ${CMAKE_CURRENT_BINARY_DIR}/kernels_ispc.h)

set(ISPC_TARGET_FLAG "")
if(ADSB_ISPC_TARGET)
    set(ISPC_TARGET_FLAG "--target=${ADSB_ISPC_TARGET}")
endif()

add_custom_command(
    OUTPUT ${ISPC_OBJ} ${ISPC_HEADER}
    COMMAND ${ISPC_EXECUTABLE} ${ISPC_SRC} -O2 --pic ${ISPC_TARGET_FLAG} -o ${ISPC_OBJ} -h ${ISPC_HEADER}
    DEPENDS ${ISPC_SRC}
    COMMENT "ispc: compiling kernels.ispc"
)
add_custom_target(ispc_kernels DEPENDS ${ISPC_OBJ} ${ISPC_HEADER})

add_executable(adsb
    src/main.cpp
    src/demod.cpp
    src/filter_bank.cpp
    src/sigmf_meta.cpp
    src/frame_decode.cpp
    src/aircraft.cpp
    src/pipeline.cpp
    src/ws_publisher.cpp
    src/aircraft_history.cpp
    src/spectrum.cpp
    src/rtlsdr_stream.cpp
    src/file_stream.cpp
    src/tasksys.cpp
    ${ISPC_OBJ}
)
add_dependencies(adsb ispc_kernels)

target_include_directories(adsb PRIVATE
    include
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_compile_definitions(adsb PRIVATE ADSB_SAMPLES_PER_SYMBOL=${ADSB_SAMPLES_PER_SYMBOL})

find_package(Threads REQUIRED)
target_link_libraries(adsb PRIVATE Threads::Threads coro::coro librtlsdr::librtlsdr argparse::argparse
                                    nlohmann_json::nlohmann_json fftw::fftw SQLite::SQLite3)

install(TARGETS adsb RUNTIME DESTINATION bin)
