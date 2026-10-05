# Libraries in dependency order -- each only links ones listed above it:
#   common -> dsp -> decode -> input, output -> apps
# sim depends only on common, so the simulator stays independent of the
# decoder (its tests link decode separately to check against it).
add_subdirectory(common)
add_subdirectory(dsp)
add_subdirectory(decode)
add_subdirectory(input)
add_subdirectory(output)
add_subdirectory(sim)
add_subdirectory(apps)
