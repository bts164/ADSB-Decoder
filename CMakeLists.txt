cmake_minimum_required(VERSION 3.20)
project(adsb CXX)

set(CMAKE_CXX_STANDARD 23)  # coro requires C++23 (std::expected, etc.)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Bounds checks in a Debug build: std::span / std::vector operator[] and friends (libstdc++), and xtensor's
# element access and shape checks. Release leaves them out, so hot paths check sizes once up front instead.
add_compile_definitions($<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS> $<$<CONFIG:Debug>:XTENSOR_ENABLE_ASSERT>)

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
find_package(xtensor REQUIRED)
find_package(HighFive REQUIRED)
find_package(absl REQUIRED)

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

set(ISPC_TARGET_FLAG "")
if(ADSB_ISPC_TARGET)
    set(ISPC_TARGET_FLAG "--target=${ADSB_ISPC_TARGET}")
endif()

# Compiles one .ispc file into an object + generated header in the calling
# directory's binary dir; `target` gets both (the header dir PRIVATE, since
# only that library's own .cpp includes it).
function(adsb_add_ispc target src)
    get_filename_component(name ${src} NAME_WE)
    set(obj ${CMAKE_CURRENT_BINARY_DIR}/${name}_ispc.o)
    set(hdr ${CMAKE_CURRENT_BINARY_DIR}/${name}_ispc.h)
    add_custom_command(
        OUTPUT ${obj} ${hdr}
        COMMAND ${ISPC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/${src} -O2 --pic ${ISPC_TARGET_FLAG} -o ${obj} -h ${hdr}
        DEPENDS ${src}
        COMMENT "ispc: compiling ${src}"
    )
    target_sources(${target} PRIVATE ${obj} ${hdr})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()

find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED)

# One static library per src/ subdirectory (adsb_common, adsb_dsp, ...);
# headers sit next to their .cpp and are included as "dir/name.h" with src/
# as the include root. See src/CMakeLists.txt for the dependency order.
# Executables (apps + tests) all land at the build root, e.g. build/Release/adsb.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
add_subdirectory(tools)
