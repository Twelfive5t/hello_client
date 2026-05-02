/// @file include/hello_client/common.hpp
/// @brief hello_client DLL 公共头文件，定义导出宏和基础类型

#pragma once

#include <cstdint>

#if defined(_MSC_VER)
#ifdef HELLO_CLIENT_EXPORT_DLL
#define HELLO_CLIENT_API __declspec(dllexport)
#else
#define HELLO_CLIENT_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define HELLO_CLIENT_API __attribute__((visibility("default")))
#else
#define HELLO_CLIENT_API
#endif

namespace hello_client {

/// @brief 错误码枚举
enum class client_error : std::uint8_t {
    K_OK = 0,
    K_NOT_INITIALIZED,   ///< 未初始化（未调用 Init）
    K_CONNECTION_FAILED, ///< 连接失败
    K_RPC_FAILED,        ///< RPC 调用失败
};

} // namespace hello_client
