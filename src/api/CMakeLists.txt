# The UI API's messages and services, generated from proto/adsb/api/v1/api.proto, and the conversions to them.
# Included as "adsb/api/v1/api.pb.h" and "adsb/api/v1/api.wsrpc.h", from the generated tree's root.
add_library(adsb_api STATIC convert.cpp)
protobuf_generate(
    TARGET adsb_api
    PROTOS ${PROJECT_SOURCE_DIR}/proto/adsb/api/v1/api.proto
    IMPORT_DIRS ${PROJECT_SOURCE_DIR}/proto
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)
wsrpc_generate(
    TARGET adsb_api
    PROTOS ${PROJECT_SOURCE_DIR}/proto/adsb/api/v1/api.proto
    IMPORT_DIRS ${PROJECT_SOURCE_DIR}/proto
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)
target_include_directories(adsb_api PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(adsb_api PUBLIC adsb_wsrpc adsb_decode adsb_dsp protobuf::libprotobuf)
