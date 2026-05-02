cmake_minimum_required(VERSION 3.22.1)

# =================== 包含模块文件 ===================
# 添加 cmake 模块路径
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# 导入版本模块配置（需在 project() 前执行）
include(VersionConfig)

# =================== 项目基本信息 ===================
project(hello_client VERSION ${VERSION})

# 导入通用模块配置
include(CommonConfig)
include(ClangTools)

# =================== 子目录 ===================
add_subdirectory(${CMAKE_SOURCE_DIR}/services/proto messages)
add_subdirectory(services/client)

# Custom command to move compile_commands.json to a fixed path
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_target(copy_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/build/compile_commands.json
        COMMENT "Copying compile_commands.json to build"
    )
endif()
