set(protobuf_MODULE_COMPATIBLE TRUE)

find_package(absl CONFIG REQUIRED)

find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")
set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)

find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")
set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)

# Proto files
set(protofiles "server_service" "update_service")

foreach(protofile ${protofiles})
    set(proto_target "${protofile}_messages")

    get_filename_component(hw_proto "${CMAKE_CURRENT_SOURCE_DIR}/${protofile}.proto" ABSOLUTE)
    get_filename_component(hw_proto_path "${hw_proto}" PATH)
    set(hw_proto_cpp_path "${CMAKE_CURRENT_BINARY_DIR}")

    # Generated sources
    set(hw_proto_srcs "${hw_proto_cpp_path}/${protofile}.pb.cc")
    set(hw_proto_hdrs "${hw_proto_cpp_path}/${protofile}.pb.h")
    set(hw_grpc_srcs "${hw_proto_cpp_path}/${protofile}.grpc.pb.cc")
    set(hw_grpc_hdrs "${hw_proto_cpp_path}/${protofile}.grpc.pb.h")

    add_custom_command(
        OUTPUT "${hw_proto_srcs}" "${hw_proto_hdrs}" "${hw_grpc_srcs}" "${hw_grpc_hdrs}"
        COMMAND ${_PROTOBUF_PROTOC}
        ARGS --grpc_out "${hw_proto_cpp_path}"
        --cpp_out "${hw_proto_cpp_path}"
        -I "${hw_proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${hw_proto}"
        DEPENDS "${hw_proto}")

    add_library(${proto_target} STATIC
        ${hw_proto_srcs}
        ${hw_grpc_srcs})
    target_link_libraries(${proto_target} PUBLIC
        gRPC::grpc++_reflection
        gRPC::grpc++
        protobuf::libprotobuf)
    target_include_directories(${proto_target} SYSTEM INTERFACE
        $<BUILD_INTERFACE:${hw_proto_cpp_path}>
    )
    set_target_properties(${proto_target} PROPERTIES
        CXX_CLANG_TIDY ""
    )
endforeach()
