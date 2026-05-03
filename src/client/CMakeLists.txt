find_package(spdlog CONFIG REQUIRED)

add_library(hello_client SHARED
    src/hello_client.cpp
    src/control/client_impl.cpp
)

target_include_directories(hello_client
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(hello_client
    PRIVATE
    spdlog::spdlog
    client_messages
    TelemetryLib
)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(hello_client PRIVATE HELLO_CLIENT_EXPORT_DLL)
    set_property(TARGET hello_client PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

install(TARGETS hello_client DESTINATION bin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include)
