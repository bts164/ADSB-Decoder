#pragma once

#include <string>
#include <string_view>
#include <vector>

/**
 * @file
 * What went into this build, for each app's --version (apps/version_options.h) and its first log line: the
 * source's version from git, the compiler, and the Conan profiles and dependency graph it was built with.
 *
 * The git version is `git describe --always --dirty --tags` of the source tree, taken on every build
 * (version_info_git.cmake). A package built by `conan create` has no .git, so it gets the tree's version when
 * conanfile.py's export() ran. The Conan part is written by conanfile.py's generate() at `conan install`.
 */

namespace adsb::version_info {

/** How much --version prints. Each text level adds to the one before; json holds all of it. */
enum class Level { summary, deps, full, json };

/** A library loaded at run time, with the version it reports, or why it couldn't be asked. */
struct RuntimeLibrary {
    std::string name;
    std::string version;
};

/** `git describe` of the source this was built from, such as "7ee8f71-dirty"; "unknown" without git. */
std::string_view git_describe();

/** "<git describe>, <build type> build, <compiler>, coro/<version>", for an app's first log line. */
std::string summary_line();

/**
 * The --version report at `level`: `program`'s version and compiler, the Conan part at that level, and
 * `runtime`. full and json also list every shared object loaded, with its path, which shows which libcoro.so or
 * libuhd.so the dynamic linker picked.
 */
std::string report(std::string_view program, Level level, const std::vector<RuntimeLibrary>& runtime);

/** Defined in the sources conanfile.py's generate() writes, one per Level. */
namespace generated {
std::string_view conan_summary();
std::string_view conan_deps();
std::string_view conan_full();
std::string_view conan_json();
}  // namespace generated

}  // namespace adsb::version_info
