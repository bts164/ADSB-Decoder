from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class AdsbRecipe(ConanFile):
    name = "adsb"
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"
    exports_sources = "CMakeLists.txt", "include/*", "src/*", "ispc/*"

    # No version attribute: this is developed and re-exported/rebuilt as
    # often as coro is (see requirements() below), so pass --version
    # explicitly on `conan create` rather than hand-editing a pinned value
    # here after every change. `conan build .` (local build, no packaging)
    # doesn't need a version at all.

    def requirements(self):
        # coro is developed alongside this project (../../coro) and
        # re-exported to the local cache often, so pinning an exact version
        # here would just mean hand-editing this file after every
        # `conan create` there. A version range instead always resolves to
        # whatever's newest in the cache (or conan-devel, once a build
        # doesn't have a newer local export) -- see coro/conanfile.py's own
        # versioning comment for why pinning matters more once coro's API
        # actually stabilizes, which isn't yet.
        self.requires("coro/[*]")
        # Local-cache-only recipe from ../conan-recipes/librtlsdr (not yet
        # on any remote) -- quick smoke test that we can link against it and
        # call into the driver; see main.cpp.
        self.requires("librtlsdr/2.0.3")
        self.requires("argparse/3.2")
        self.requires("nlohmann_json/3.11.3")
        # Waterfall/spectrum view: FFT over raw pre-resample IQ blocks in
        # --rtlsdr mode (see main.cpp's run_rtlsdr_stream). float precision
        # only -- fftw_options below drops the double/long-double builds
        # nothing here uses.
        self.requires("fftw/3.3.10")

    def configure(self):
        # Only the single-precision (float) library is used -- see
        # requirements() -- so skip building fftw's double/long-double/quad
        # variants entirely.
        self.options["fftw"].precision_double = False
        self.options["fftw"].precision_longdouble = False

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
