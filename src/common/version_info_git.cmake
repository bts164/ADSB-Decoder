# Writes OUT from TEMPLATE with the source tree's `git describe`. Run with cmake -P on every build (see
# CMakeLists.txt), so the version follows each commit and edit. Without a .git, as in the Conan cache, it uses
# EXPORTED, what conanfile.py's export() recorded. configure_file() leaves OUT alone when it wouldn't change, so an
# unchanged tree recompiles nothing.
#
# Takes SOURCE_DIR, GIT (the git executable, possibly empty), EXPORTED, TEMPLATE and OUT.
set(describe "")
if(GIT AND EXISTS "${SOURCE_DIR}/.git")
    execute_process(COMMAND "${GIT}" describe --always --dirty --tags
                    WORKING_DIRECTORY "${SOURCE_DIR}"
                    OUTPUT_VARIABLE describe OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE result ERROR_QUIET)
    if(NOT result EQUAL 0)
        set(describe "")
    endif()
endif()
if(describe STREQUAL "")
    set(describe "${EXPORTED}")
endif()
if(describe STREQUAL "")
    set(describe "unknown")
endif()
configure_file("${TEMPLATE}" "${OUT}" @ONLY)
