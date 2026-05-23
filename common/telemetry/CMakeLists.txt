project(TelemetryLib)

find_package(opentelemetry-cpp CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

add_library(TelemetryLib STATIC
    telemetry.cpp
    telemetry_internal.cpp
    trace_pipeline.cpp
    trace_span.cpp
    grpc_metrics.cpp
)

target_include_directories(TelemetryLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(TelemetryLib PUBLIC
    opentelemetry-cpp::opentelemetry-cpp
    gRPC::grpc++
)
