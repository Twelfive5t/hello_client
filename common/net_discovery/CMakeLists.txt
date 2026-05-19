project(NetDiscoveryLib)

find_package(mdns CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_library(NetDiscoveryLib STATIC net_discovery.cpp)

target_include_directories(NetDiscoveryLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(NetDiscoveryLib PUBLIC mdns::mdns spdlog::spdlog)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(NetDiscoveryLib PUBLIC ws2_32)
endif()
