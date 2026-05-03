cmake_minimum_required(VERSION 3.22.1)

# =================== 包含模块文件 ===================
# 添加 cmake 模块路径
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# 导入版本模块配置（需在 project() 前执行）
include(VersionConfig)

# =================== 项目基本信息 ===================
project(hello_client VERSION ${VERSION})

# =================== 配置选项 ======================
option(ENABLE_TESTING "Enable testing" OFF)

# 导入通用模块配置
include(CommonConfig)
include(ClangTools)

# =================== 子目录 ===================
add_subdirectory(src/proto)
add_subdirectory(src/messages)
add_subdirectory(src/client)

# =================== 测试配置 ===================
if(ENABLE_TESTING)
    add_subdirectory(test)
endif()
