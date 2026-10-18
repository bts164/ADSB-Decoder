# The VITA-49 (VRT) wire format (doc/design/vita49-format.md): encoding for vita49_send, reordering and
# gap-filling for `adsb vita49`. Pure logic with no I/O.
add_library(adsb_vrt STATIC encode.cpp assembler.cpp)
target_link_libraries(adsb_vrt PUBLIC adsb_common xtensor)
