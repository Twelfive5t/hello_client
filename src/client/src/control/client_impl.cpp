/// @file src/client/src/control/client_impl.cpp
/// @brief client_service PIMPL 实现 —— 调用 messages 层，不直接依赖 gRPC/protobuf

#include "services/client_service.hpp"
#include "messages/client_messages.hpp"

#include <spdlog/spdlog.h>
#include <memory>

namespace hello_client {

// ---------------------------------------------------------------------------
// PIMPL 实现 —— 持有 client_messages::service_client，屏蔽 gRPC 细节
// ---------------------------------------------------------------------------
class client_service::Impl {
public:
    auto init(const std::string &ip_port) noexcept -> client_error
    {
        try {
            client_ = std::make_shared<client_messages::service_client>(ip_port);
            ip_port_ = ip_port;
            return client_error::K_OK;
        } catch (...) {
            spdlog::error("client_service::init failed to create service_client");
            return client_error::K_CONNECTION_FAILED;
        }
    }

    auto check_online() noexcept -> client_error
    {
        if (!client_) {
            spdlog::error("client_service not initialized, call init() first");
            return client_error::K_NOT_INITIALIZED;
        }
        return client_->check_online();
    }

private:
    std::shared_ptr<client_messages::service_client> client_;
    std::string ip_port_;
};

// ---------------------------------------------------------------------------
// client_service 公共接口
// ---------------------------------------------------------------------------
client_service::client_service() : pimpl_(std::make_unique<Impl>()) {}

client_service::~client_service() = default;

auto client_service::init(const std::string &ip_port) noexcept -> client_error
{
    return pimpl_->init(ip_port);
}

auto client_service::check_online() noexcept -> client_error
{
    return pimpl_->check_online();
}

} // namespace hello_client
