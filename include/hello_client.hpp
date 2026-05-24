/// @file include/hello_client/hello_client.hpp
/// @brief hello_client 顶层封装类（PIMPL 模式）

#pragma once

#include "common.hpp"
#include "services/client_service.hpp"
#include "services/update_service.hpp"

#include <memory>
#include <string>

namespace hello_client
{

/// @brief hello_client SDK 入口类
///
/// 封装所有对服务器的 gRPC 通信，内部通过具体 service 完成实际调用。
/// 使用 PIMPL 模式隐藏实现细节。调用任何接口前须先调用 init()。
class HELLO_CLIENT_API hello_client
{
public:
    hello_client();
    ~hello_client();

    hello_client(const hello_client &) = delete;
    auto operator=(const hello_client &) -> hello_client & = delete;

    hello_client(hello_client &&) = delete;
    auto operator=(hello_client &&) -> hello_client & = delete;

    /// @brief 初始化，建立到服务器的 gRPC channel 并启动 Trace 导出
    /// @param ip_port         服务器地址，格式 "ip:port"，例如 "127.0.0.1:50051"
    /// @return client_error::K_OK 表示成功
    auto create(const std::string &ip_port) noexcept -> client_error;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;

public:
    client_service client;
    update_service update;
};

} // namespace hello_client
