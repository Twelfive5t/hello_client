/// @file include/hello_client/client_service.hpp
/// @brief 封装 ServerMessages gRPC 服务的客户端类（PIMPL 模式）

#pragma once

#include "hello_client/common.hpp"
#include <memory>
#include <string>

namespace hello_client {

/// @brief 封装对 ServerMessagesService gRPC 接口的调用
///
/// 使用 PIMPL 模式隐藏 gRPC 实现细节。调用任何接口前须先调用 init()。
class HELLO_CLIENT_API client_service {
  public:
  client_service();
    ~client_service();

    /// @brief 初始化，建立到服务器的 gRPC channel
    /// @param ip_port 服务器地址，格式 "ip:port"，例如 "127.0.0.1:50051"
    /// @return client_error::K_OK 表示成功
    auto init(const std::string &ip_port) noexcept -> client_error;

    /// @brief 检查服务器是否在线
    /// @return client_error::K_OK 表示服务器在线
    auto check_online() noexcept -> client_error;

  private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace hello_client
