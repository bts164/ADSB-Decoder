import os
from pathlib import Path

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get
from conan.tools.system.package_manager import Apt


class LibrtlsdrRecipe(ConanFile):
    name = "librtlsdr"
    version = "2.0.3"
    package_type = "library"
    license = "GPL-2.0-or-later"
    url = "https://github.com/osmocom/rtl-sdr"
    description = "Driver for Realtek RTL2832U based SDR receivers (osmocom rtl-sdr)"

    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        # Mirrors the two runtime-behavior options upstream's CMakeLists.txt
        # exposes (see build()); INSTALL_UDEV_RULES is deliberately not
        # exposed here -- it copies rules into /etc/udev/rules.d, a system
        # path that has no business being touched by a Conan package build.
        "detach_kernel_driver": [True, False],
        "enable_zerocopy": [True, False],
        # Forwards to libusb's own enable_udev option (see requirements()).
        # Off by default so a plain `conan create` never needs a system
        # package; flip to True (with -o and, the first time, also
        # -c tools.system.package_manager:mode=install so Apt is actually
        # allowed to install libudev-dev -- see system_requirements()) to
        # try the udev-backed libusb build instead.
        "with_udev": [True, False],
    }
    default_options = {
        "shared": True,
        "fPIC": True,
        # Matches upstream's own CMakeLists.txt defaults (both OFF) --
        # zerocopy in particular is still marked experimental upstream.
        "detach_kernel_driver": False,
        "enable_zerocopy": False,
        "with_udev": False,
    }

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        # enable_udev forwards to libusb's own option, which gates whether
        # it takes a transitive libudev/system dependency at all. udev only
        # affects libusb's device enumeration method and hotplug callback
        # support (LIBUSB_ERROR_NOT_SUPPORTED without it) -- librtlsdr never
        # calls the hotplug API, it just enumerates and opens a device
        # synchronously, so with_udev=False (the default) loses nothing
        # rtl-sdr itself uses; see system_requirements() for the libudev-dev
        # header package this needs when with_udev=True.
        self.requires("libusb/1.0.29", transitive_headers=True, transitive_libs=True,
                       options={"enable_udev": bool(self.options.with_udev)})

    def system_requirements(self):
        # Only touches the system when with_udev is explicitly enabled --
        # and even then, Apt.install() itself still defers to
        # tools.system.package_manager:mode (default "check", which reports
        # what's missing instead of installing it), so this alone can't
        # silently run a privileged install; the caller must also pass
        # -c tools.system.package_manager:mode=install to actually opt in.
        if self.options.with_udev:
            Apt(self).install(["libudev-dev"])

    def source(self):
        get(self, f"https://github.com/osmocom/rtl-sdr/archive/refs/tags/v{self.version}.tar.gz", strip_root=True)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.cache_variables["DETACH_KERNEL_DRIVER"] = bool(self.options.detach_kernel_driver)
        tc.cache_variables["ENABLE_ZEROCOPY"] = bool(self.options.enable_zerocopy)
        tc.cache_variables["INSTALL_UDEV_RULES"] = False
        # Upstream's src/CMakeLists.txt (see comment in package()) always
        # defines both the shared (`rtlsdr`) and static (`rtlsdr_static`)
        # library targets regardless of BUILD_SHARED_LIBS -- this only
        # affects the 8 CLI tools (rtl_sdr, rtl_tcp, ...), which link
        # against convenience_static either way. Forcing PIC unconditionally
        # keeps the static variant embeddable into a future shared consumer
        # without a second, PIC-only build.
        tc.cache_variables["CMAKE_POSITION_INDEPENDENT_CODE"] = True
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "COPYING", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        cmake = CMake(self)
        cmake.install()
        # Upstream always builds+installs both rtlsdr (shared) and
        # rtlsdr_static, plus the CLI tools (rtl_sdr, rtl_tcp, rtl_test,
        # rtl_fm, rtl_eeprom, rtl_adsb, rtl_power, rtl_biast) -- there's no
        # CMake option to build only one library variant. Rather than patch
        # upstream's CMakeLists.txt, just drop whichever library variant
        # `shared` didn't ask for after cmake.install() has already placed
        # both, so package_info() below can report a single, option-
        # consistent `rtlsdr` library name either way.
        lib_dir = Path(self.package_folder) / "lib"
        if self.options.shared:
            for stale in list(lib_dir.glob("librtlsdr_static.a")) + list(lib_dir.glob("librtlsdr.a")):
                stale.unlink()
        else:
            for stale in lib_dir.glob("librtlsdr.so*"):
                stale.unlink()
            static_lib = lib_dir / "librtlsdr_static.a"
            if static_lib.exists():
                static_lib.rename(lib_dir / "librtlsdr.a")

    def package_info(self):
        self.cpp_info.libs = ["rtlsdr"]
        self.cpp_info.set_property("pkg_config_name", "librtlsdr")
        if self.settings.os in ("Linux", "FreeBSD"):
            self.cpp_info.system_libs = ["pthread", "m"]
