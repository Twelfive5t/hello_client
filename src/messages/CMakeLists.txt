find_package(spdlog CONFIG REQUIRED)

add_library(client_messages STATIC client_messages.cpp)

target_include_directories(client_messages
    PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(client_messages
    PUBLIC
    server_messages
    PRIVATE
    spdlog::spdlog
    TelemetryLib
)
