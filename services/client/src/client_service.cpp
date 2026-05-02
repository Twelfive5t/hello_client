/// @file services/client/src/client_service.cpp
/// @brief ClientService 及其 PIMPL 实现

#include "hello_client/client_service.hpp"
#include "server_messages.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <memory>
#include <spdlog/spdlog.h>

namespace hello_client
{

// ---------------------------------------------------------------------------
// PIMPL 实现类
// ---------------------------------------------------------------------------
class client_service::Impl
{
public:
    auto init(const std::string &ip_port) noexcept -> client_error
    {
        try {
            auto channel = grpc::CreateChannel(ip_port, grpc::InsecureChannelCredentials());
            stub_ = ServerMessages::ServerMessagesService::NewStub(channel);
            ip_port_ = ip_port;
            return client_error::K_OK;
        } catch (...) {
            return client_error::K_CONNECTION_FAILED;
        }
    }

    auto check_online() noexcept -> client_error
    {
        if (!stub_) {
            spdlog::error("client_service not initialized, call init() first");
            return client_error::K_NOT_INITIALIZED;
        }

        grpc::ClientContext context;
        const ServerMessages::CheckOnlineRequest request{};
        ServerMessages::CheckOnlineReply reply;

        const grpc::Status status = stub_->CheckOnline(&context, request, &reply);

        if (status.ok()) {
            spdlog::info("check_online: server is online (" + ip_port_ + ")");
            return client_error::K_OK;
        }

        spdlog::error("check_online RPC failed: " + status.error_message());
        return client_error::K_RPC_FAILED;
    }

private:
    std::unique_ptr<ServerMessages::ServerMessagesService::Stub> stub_;
    std::string ip_port_;
};

// ---------------------------------------------------------------------------
// client_service 公共接口
// ---------------------------------------------------------------------------
client_service::client_service() : pimpl_(std::make_unique<Impl>())
{
}

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
