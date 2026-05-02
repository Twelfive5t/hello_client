/// @file src/messages/client_messages.hpp
/// @brief 私有 gRPC stub 封装层 —— 不对外安装，仅供 control 层使用

#pragma once

#include "common.hpp"
#include <memory>
#include <string>

namespace client_messages {

/// @brief 封装 ServerMessagesService gRPC stub 的原始调用
///
/// 此类仅在库内部使用，不导出，不安装。
/// 上层 control 通过此类调用 gRPC，屏蔽所有 protobuf/grpc 头文件依赖。
class service_client {
public:
    explicit service_client(const std::string &ip_port);
    ~service_client();

    service_client(const service_client &) = delete;
    auto operator=(const service_client &) -> service_client & = delete;
    service_client(service_client &&) = delete;
    auto operator=(service_client &&) -> service_client & = delete;

    /// @brief 检查服务器是否在线（1s 超时）
    [[nodiscard]] auto check_online() const noexcept -> hello_client::client_error;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace client_messages