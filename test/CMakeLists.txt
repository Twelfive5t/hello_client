enable_testing()
add_compile_definitions(GTEST_ENABLED)

find_package(GTest CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(main_test
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main_test.cpp
)

target_include_directories(main_test
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(main_test PRIVATE hello_client NetDiscoveryLib GTest::gtest)
target_compile_features(main_test PRIVATE cxx_std_20)

add_test(NAME main_test COMMAND main_test --gtest_color=yes)

# =================== 安装配置 ===================
set(HELLO_CLIENT_TEST_BAT "${CMAKE_SOURCE_DIR}/tools/hello_client_test.bat")

install(TARGETS main_test DESTINATION bin)

install(CODE "
    message(STATUS \"Generating hello_client_test.bat...\")
    execute_process(
        COMMAND \"${Python3_EXECUTABLE}\" \"${CMAKE_SOURCE_DIR}/tools/generate_windows_test_bat.py\"
        WORKING_DIRECTORY \"${CMAKE_SOURCE_DIR}/tools\"
        RESULT_VARIABLE _gen_result
    )
    if(NOT _gen_result EQUAL 0)
        message(FATAL_ERROR \"Failed to generate hello_client_test.bat\")
    endif()
")

install(FILES "${HELLO_CLIENT_TEST_BAT}" DESTINATION bin OPTIONAL)
