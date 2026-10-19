# Libraries in dependency order -- each only links ones listed above it:
#   common -> dsp -> decode -> api -> input, output -> apps
# sim depends only on common, so the simulator stays independent of the
# decoder (its tests link decode separately to check against it).
# net (UDP sending helpers) and vrt (the VITA-49 wire format, used by input
# and vita49_send) also depend only on common. api holds the UI API's
# generated protobuf messages and the conversions to them from decode's and
# dsp's types.
add_subdirectory(common)
add_subdirectory(net)
add_subdirectory(vrt)
add_subdirectory(dsp)
add_subdirectory(decode)
add_subdirectory(api)
add_subdirectory(input)
add_subdirectory(output)
add_subdirectory(sim)
add_subdirectory(apps)
