/// @file src/client/src/control/update_impl.cpp
/// @brief update_service PIMPL 实现 —— 调用 messages 层，不直接依赖 gRPC/protobuf

#include "messages/client_messages.hpp"
#include "services/update_service.hpp"

#include <spdlog/spdlog.h>

#include <memory>

namespace hello_client
{

class update_service::Impl
{
public:
    auto init(const std::string &ip_port) noexcept -> client_error
    {
        try {
            client_ = std::make_shared<client_messages::update_service_client>(ip_port);
            ip_port_ = ip_port;
            return client_error::K_OK;
        } catch (...) {
            spdlog::error("update_service::init failed to create update_service_client");
            return client_error::K_CONNECTION_FAILED;
        }
    }

    auto update_server(const std::string &package_path) noexcept -> client_error
    {
        if (!client_) {
            spdlog::error("update_service not initialized, call init() first");
            return client_error::K_NOT_INITIALIZED;
        }
        return client_->update_server(package_path);
    }

    auto get_update_status(update_status_info &info) noexcept -> client_error
    {
        if (!client_) {
            spdlog::error("update_service not initialized, call init() first");
            return client_error::K_NOT_INITIALIZED;
        }
        return client_->get_update_status(info);
    }

private:
    std::shared_ptr<client_messages::update_service_client> client_;
    std::string ip_port_;
};

update_service::update_service() : pimpl_(std::make_unique<Impl>())
{
}

update_service::~update_service() = default;

auto update_service::init(const std::string &ip_port) noexcept -> client_error
{
    return pimpl_->init(ip_port);
}

auto update_service::update_server(const std::string &package_path) noexcept -> client_error
{
    return pimpl_->update_server(package_path);
}

auto update_service::get_update_status(update_status_info &info) noexcept -> client_error
{
    return pimpl_->get_update_status(info);
}

} // namespace hello_client
