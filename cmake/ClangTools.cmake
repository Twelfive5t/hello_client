# clang-tidy：全局自动启用，此行之后创建的所有 target 自动继承
option(ENABLE_CLANG_TIDY "Run clang-tidy during compilation" ON)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)

    if(CLANG_TIDY_EXE)
        set(CLANG_TIDY_ARGS
            "${CLANG_TIDY_EXE}"
            "--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy"
        )

        # 仅在 MSVC 下追加参数
        if(MSVC)
            list(APPEND CLANG_TIDY_ARGS
                "--extra-arg=/EHsc"
                "--extra-arg=/Fonul"
            )
        endif()

        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_ARGS}")

        message(STATUS "clang-tidy enabled: ${CLANG_TIDY_ARGS}")
    else()
        message(WARNING "clang-tidy not found, static analysis disabled")
    endif()
endif()

# clang-format：手动目标，格式化 services/client 下所有源文件
find_program(CLANG_FORMAT_EXE NAMES clang-format)

if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE ALL_SOURCE_FILES
        "${CMAKE_SOURCE_DIR}/services/*.cpp"
        "${CMAKE_SOURCE_DIR}/services/*.hpp"
        "${CMAKE_SOURCE_DIR}/include/*.cpp"
        "${CMAKE_SOURCE_DIR}/include/*.hpp"
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i --style=file ${ALL_SOURCE_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-format: formatting all project sources"
        VERBATIM
    )
    message(STATUS "clang-format enabled: ${CLANG_FORMAT_EXE}")
else()
    message(WARNING "clang-format not found, formatting disabled")
endif()
