# The wsrpc runtime: calls to protobuf services over one WebSocket connection, framed by
# proto/wsrpc/v1/wsrpc.proto. Included as "wsrpc/serve.h", with the framing's messages as "wsrpc/v1/wsrpc.pb.h".
add_library(adsb_wsrpc STATIC serve.cpp)
protobuf_generate(
    TARGET adsb_wsrpc
    PROTOS ${PROJECT_SOURCE_DIR}/proto/wsrpc/v1/wsrpc.proto
    IMPORT_DIRS ${PROJECT_SOURCE_DIR}/proto
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)
target_include_directories(adsb_wsrpc PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(adsb_wsrpc PUBLIC adsb_common coro::coro protobuf::libprotobuf)

# The plugin, tools/protoc-gen-wsrpc, is Python. protoc runs a plugin by path, so it runs it through a wrapper
# that names the interpreter found here, whatever the script's #! line would find.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import google.protobuf"
    RESULT_VARIABLE wsrpc_python_protobuf
    OUTPUT_QUIET ERROR_QUIET
)
if(NOT wsrpc_python_protobuf EQUAL 0)
    message(FATAL_ERROR "tools/protoc-gen-wsrpc needs the Python protobuf package, which ${Python3_EXECUTABLE} "
                        "can't import. A Conan build sets up a venv that has it, at protoc's version; otherwise, "
                        "install it with `${Python3_EXECUTABLE} -m pip install protobuf`, or point "
                        "-DPython3_EXECUTABLE at an interpreter that has it")
endif()
file(WRITE ${PROJECT_BINARY_DIR}/protoc-gen-wsrpc
     "#!/bin/sh\nexec \"${Python3_EXECUTABLE}\" \"${PROJECT_SOURCE_DIR}/tools/protoc-gen-wsrpc\" \"$@\"\n")
file(CHMOD ${PROJECT_BINARY_DIR}/protoc-gen-wsrpc
     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Generates each of PROTOS' services' C++, <proto>.wsrpc.h and .wsrpc.cc, into PROTOC_OUT_DIR, and adds it to
# TARGET, which also needs the messages' C++ (protobuf_generate) and adsb_wsrpc.
function(wsrpc_generate)
    cmake_parse_arguments(arg "" "TARGET;PROTOC_OUT_DIR" "PROTOS;IMPORT_DIRS" ${ARGN})
    protobuf_generate(
        TARGET ${arg_TARGET}
        LANGUAGE wsrpc
        PLUGIN protoc-gen-wsrpc=${PROJECT_BINARY_DIR}/protoc-gen-wsrpc
        PLUGIN_OPTIONS lang=cpp
        GENERATE_EXTENSIONS .wsrpc.h .wsrpc.cc
        PROTOS ${arg_PROTOS}
        IMPORT_DIRS ${arg_IMPORT_DIRS}
        PROTOC_OUT_DIR ${arg_PROTOC_OUT_DIR}
        DEPENDENCIES ${PROJECT_SOURCE_DIR}/tools/protoc-gen-wsrpc
    )
endfunction()
