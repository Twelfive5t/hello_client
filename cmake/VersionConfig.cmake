# =================== 版本配置 ===================

# 读取版本文件
file(READ "version.txt" VERSION_CONTENTS)
string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[a-zA-Z]+(\\.[0-9]+)*)?" VERSION_MATCH ${VERSION_CONTENTS})

# 提取版本号
set(VERSION_MAJOR ${CMAKE_MATCH_1})
set(VERSION_MINOR ${CMAKE_MATCH_2})
set(VERSION_PATCH ${CMAKE_MATCH_3})

# 尝试提取扩展版本号
string(REGEX MATCH "([a-zA-Z]+(\\.[0-9]+)*)" VERSION_EXTENT ${VERSION_MATCH})

set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

# 是否存在扩展版本号
if(VERSION_EXTENT)
    set(VERSION_RELEASE ${VERSION}-${VERSION_EXTENT})
else()
    set(VERSION_RELEASE ${VERSION})
endif()

message(STATUS "Version: ${VERSION_RELEASE}")
