/// @file include/hello_client/update_service.hpp
/// @brief 封装 UpdateService gRPC 服务的客户端类（PIMPL 模式）

#pragma once

#include "common.hpp"

#include <memory>
#include <string>

namespace hello_client
{

/// @brief 封装对 UpdateService gRPC 接口的调用
///
/// 使用 PIMPL 模式隐藏 gRPC 实现细节。调用任何接口前须先调用 init()。
class HELLO_CLIENT_API update_service
{
public:
    update_service();
    ~update_service();

    /// @brief 初始化，建立到升级服务的 gRPC channel
    /// @param ip_port 升级服务地址，格式 "ip:port"，例如 "127.0.0.1:50052"
    /// @return client_error::K_OK 表示成功
    auto init(const std::string &ip_port) noexcept -> client_error;

    /// @brief 上传并触发服务器 OTA 升级包
    /// @param package_path OTA tar.gz 包路径
    /// @return client_error::K_OK 表示升级 RPC 完成且服务端返回成功
    auto update_server(const std::string &package_path) noexcept -> client_error;

    /// @brief 获取当前 OTA 升级状态
    /// @param info 输出升级状态快照
    /// @return client_error::K_OK 表示获取成功
    auto get_update_status(update_status_info &info) noexcept -> client_error;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace hello_client
