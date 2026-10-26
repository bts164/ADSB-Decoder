import os
import json
from datetime import datetime, timezone

from conan import ConanFile, conan_version
from conan.errors import ConanException
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import load, save, update_conandata
from conan.tools.scm import Git
from conan.tools.system import PyEnv


class AdsbRecipe(ConanFile):
    name = "adsb"
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"
    # with_hdf5=False leaves out adsb --debug-h5 and so HDF5, which can't be cross-built. with_uhd=True adds
    # adsb uhd, for Ettus USRPs; off by default, as UHD brings Boost and is GPL-3.0.
    options = {
        "with_hdf5": [True, False],
        "with_sidekiq": [True, False],
        "with_uhd": [True, False]
    }
    default_options = {
        "with_hdf5": True,
        "with_sidekiq": False,
        "with_uhd": True
    }
    exports_sources = ("CMakeLists.txt", "src/*", "proto/*", "tests/CMakeLists.txt", "tests/*.cpp", "tests/*.proto",
                       "tools/protoc-gen-wsrpc", "scenarios/*")

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
        if self.options.with_hdf5:
            self.requires("highfive/2.10.0")
        if self.options.with_uhd:
            self.requires("uhd/4.10.0.0")
            # UHD's headers include Boost's, but its recipe doesn't pass them on. The same range as the UHD
            # recipe's, so both resolve to one Boost; headers only, as libuhd links Boost itself.
            self.requires("boost/[>=1.83.0 <2]", libs=False)
        # The UI API's messages (proto/adsb/api/v1/api.proto, doc/design/ui_api.md). The full library, not
        # lite: MessageDifferencer and the debug strings need it.
        self.requires("protobuf/7.36.2")
        # Logging (see src/common/log.h for the printf-style LOGF macros). protobuf requires Abseil too, and
        # its recipe takes any version from 20240722.0 to 20260107.1, so this one serves both.
        self.requires("abseil/[*]")

    def build_requirements(self):
        # protoc, for src/api's protobuf_generate(), at the same version as the library it generates for.
        self.tool_requires("protobuf/<host_version>")

    def configure(self):
        # Only the single-precision (float) library is used -- see
        # requirements() -- so skip building fftw's double/long-double/quad
        # variants entirely.
        self.options["fftw"].precision_double = False
        self.options["fftw"].precision_longdouble = False
        # Only HighFive's core API is used; skip its optional integrations
        # (boost in particular would be a large extra build).
        if self.options.with_hdf5:
            for opt in ("with_boost", "with_eigen", "with_xtensor", "with_opencv"):
                setattr(self.options["highfive"], opt, False)
        # A static libuhd registers its device drivers from static initializers, which a plain static link
        # drops, so it would find no devices.
        if self.options.with_uhd:
            self.options["uhd"].shared = True

    # The cache's copy of the sources has no .git, so the build can't ask git which version it is; this records
    # it here, the last point the working tree is at hand. A local `conan build .` asks git at build time instead
    # (src/common/version_info_git.cmake).
    def export(self):
        try:
            describe = Git(self, self.recipe_folder).run("describe --always --dirty --tags")
        except ConanException:
            return
        update_conandata(self, {"git_describe": describe})

    def layout(self):
        cmake_layout(self)

    def generate(self):
        CMakeDeps(self).generate()
        # tools/protoc-gen-wsrpc runs on the build machine, with the Python protobuf at protoc's version.
        pyenv = PyEnv(self)
        pyenv.install([f"protobuf=={self.dependencies.build['protobuf'].ref.version}"])
        tc = CMakeToolchain(self)
        tc.cache_variables["Python3_EXECUTABLE"] = pyenv.env_exe
        tc.cache_variables["ADSB_WITH_HDF5"] = bool(self.options.with_hdf5)
        tc.cache_variables["ADSB_WITH_SIDEKIQ"] = bool(self.options.with_sidekiq)
        tc.cache_variables["ADSB_WITH_UHD"] = bool(self.options.with_uhd)
        tc.cache_variables["ADSB_CONAN_VERSION_INFO"] = self._generate_version_info()
        tc.cache_variables["ADSB_GIT_DESCRIBE_EXPORTED"] = (self.conan_data or {}).get("git_describe", "")
        tc.generate()

    # Writes version_info_conan.cpp, the dependency graph and profile for --version (src/common/version_info.h): a
    # text for each level and the JSON. Rewritten only when it changes, so a `conan install` that changes nothing
    # rebuilds nothing. Returns its path.
    def _generate_version_info(self):
        def stamp(timestamp):
            if timestamp is None:
                return None
            return datetime.fromtimestamp(timestamp, timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")

        def name(ref):
            # user/channel only when set: Conan 2 recipes rarely use them.
            return f"{ref.name}/{ref.version}" + (f"@{ref.user}/{ref.channel}" if ref.user else "")

        def values(items):
            return {k: str(v) for k, v in items if v is not None}

        def joined(d):
            return " ".join(f"{k}={v}" for k, v in d.items())

        deps = []
        for require, dep in self.dependencies.items():
            license = dep.license
            deps.append({
                "name": name(dep.ref), "context": "build" if require.build else "host", "direct": require.direct,
                "visible": require.visible, "package_type": str(dep.package_type),
                "license": ", ".join(license) if isinstance(license, (list, tuple)) else license,
                "recipe_revision": dep.ref.revision, "recipe_timestamp": stamp(dep.ref.timestamp),
                "package_id": dep.pref.package_id, "package_revision": dep.pref.revision,
                "package_timestamp": stamp(dep.pref.timestamp),
                "settings": values(dep.settings.items()), "options": values(dep.options.items()),
                # What the package_id is computed from.
                "package_id_info": dep.info.serialize()})
        deps.sort(key=lambda d: (d["context"] != "host", d["name"]))
        host, build = values(self.settings.items()), values(self.settings_build.items())
        options = values(self.options.items())
        data = {"conan_version": str(conan_version), "settings": host, "settings_build": build,
                "options": options, "dependencies": deps}

        summary = [f"{host.get('build_type')} build for {host.get('os')} {host.get('arch')}"
                   + (f", C++{host['compiler.cppstd']}" if "compiler.cppstd" in host else ""),
                   f"options: {joined(options)}", "dependencies:"]
        summary += [f"  {d['name']}" for d in deps if d["context"] == "host" and d["direct"]]

        profile = ["host profile:"] + [f"  {k}={v}" for k, v in host.items()]
        profile += ["build profile:"] + [f"  {k}={v}" for k, v in build.items()]
        profile += [f"options: {joined(options)}"]
        groups = (("host", "dependencies"), ("build", "build tools"))

        deps_text = list(profile)
        for context, title in groups:
            deps_text.append(f"{title} (* direct):")
            for d in (d for d in deps if d["context"] == context):
                deps_text.append(f"  {'*' if d['direct'] else ' '} {d['name']}")
                if d["options"]:
                    deps_text.append(f"      {joined(d['options'])}")

        full = profile + [f"conan {conan_version}"]
        for context, title in groups:
            full.append(f"{title}:")
            for d in (d for d in deps if d["context"] == context):
                full.append(f"  {d['name']}#{d['recipe_revision']} ({d['recipe_timestamp']})")
                full.append(f"      {'direct' if d['direct'] else 'transitive'}, "
                            f"{'visible' if d['visible'] else 'not visible'}, {d['package_type']}, "
                            f"license {d['license']}")
                full.append(f"      package {d['package_id']}#{d['package_revision']} ({d['package_timestamp']})")
                for key in ("settings", "options"):
                    if d[key]:
                        full.append(f"      {key}: {joined(d[key])}")
                for require in d["package_id_info"].get("requires", []):
                    full.append(f"      requires {require}")

        texts = {"summary": summary, "deps": deps_text, "full": full}
        source = ["// Generated by adsb's conanfile.py generate(); see src/common/version_info.h.",
                  '#include "common/version_info.h"', "", "namespace adsb::version_info::generated {", ""]
        for key, text in [(k, "\n".join(v)) for k, v in texts.items()] + [("json", json.dumps(data, indent=2))]:
            assert ')adsb"' not in text
            source.append(f'std::string_view conan_{key}() {{ return R"adsb({text})adsb"; }}')
        source += ["", "}  // namespace adsb::version_info::generated", ""]
        source = "\n".join(source)
        path = os.path.join(self.generators_folder, "version_info_conan.cpp")
        if not os.path.exists(path) or load(self, path) != source:
            save(self, path, source)
        return path.replace("\\", "/")

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.bindirs = ["bin"]
