find_package(spdlog CONFIG REQUIRED)

add_executable(main
    src/hello_client.cpp
)

target_link_libraries(main PRIVATE spdlog::spdlog server_messages)

install(TARGETS main DESTINATION bin)
