from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class AdsbRecipe(ConanFile):
    name = "adsb"
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"
    exports_sources = "CMakeLists.txt", "src/*", "tests/CMakeLists.txt", "tests/*.cpp", "scenarios/*"

    # No version attribute: this is developed and re-exported/rebuilt as
    # often as coro is (see requirements() below), so pass --version
    # explicitly on `conan create` rather than hand-editing a pinned value
    # here after every change. `conan build .` (local build, no packaging)
    # doesn't need a version at all.

    def requirements(self):
        # coro is developed alongside this project (../coro) and
        # re-exported to the local cache often, so pinning an exact version
        # here would just mean hand-editing this file after every
        # `conan create` there. A version range instead always resolves to
        # whatever's newest in the cache (or conan-devel, once a build
        # doesn't have a newer local export) -- see coro/conanfile.py's own
        # versioning comment for why pinning matters more once coro's API
        # actually stabilizes, which isn't yet.
        self.requires("coro/[*]")
        # Local-cache-only recipe from conan-recipes/librtlsdr (not yet
        # on any remote) -- quick smoke test that we can link against it and
        # call into the driver; see main.cpp.
        self.requires("librtlsdr/2.0.3")
        self.requires("argparse/3.2")
        self.requires("nlohmann_json/3.11.3")
        # Waterfall/spectrum view: FFT over raw pre-resample IQ blocks in
        # rtlsdr mode (see main.cpp's run_rtlsdr_stream). float precision
        # only -- fftw_options below drops the double/long-double builds
        # nothing here uses.
        self.requires("fftw/3.3.10")
        # Aircraft position-history persistence (see aircraft_history.h) --
        # a single-file on-disk store, updated live as frames decode.
        self.requires("sqlite3/3.53.4")
        # vita49_send's packet storage (uninitialized xt::xtensor buffers);
        # also intended for the planned ADS-B message simulator there.
        self.requires("xtensor/0.25.0")
        # adsb --debug-h5: per-frame IQ + demod state for offline debugging
        # (see frame_recorder.h). HighFive is a header-only C++ wrapper over
        # the HDF5 C library.
        self.requires("highfive/2.10.0")
        # Logging (see src/common/log.h for the printf-style LOGF macros).
        self.requires("abseil/20250127.0")

    def configure(self):
        # Only the single-precision (float) library is used -- see
        # requirements() -- so skip building fftw's double/long-double/quad
        # variants entirely.
        self.options["fftw"].precision_double = False
        self.options["fftw"].precision_longdouble = False
        # Only HighFive's core API is used; skip its optional integrations
        # (boost in particular would be a large extra build).
        for opt in ("with_boost", "with_eigen", "with_xtensor", "with_opencv"):
            setattr(self.options["highfive"], opt, False)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        CMakeDeps(self).generate()
        CMakeToolchain(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.bindirs = ["bin"]
