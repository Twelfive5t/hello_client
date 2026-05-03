/// @file src/messages/client_messages.cpp
/// @brief ServerMessagesService gRPC stub 封装实现

#include "messages/client_messages.hpp"
#include "telemetry/telemetry.hpp"
#include "server_messages.grpc.pb.h"

#include <chrono>
#include <grpcpp/client_context.h>
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

namespace client_messages {

// ---------------------------------------------------------------------------
// PIMPL 实现 —— 持有 gRPC stub，所有 protobuf/grpc 细节封闭在此
// ---------------------------------------------------------------------------
class service_client::Impl {
public:
    explicit Impl(const std::string &ip_port)
    {
        auto channel = grpc::CreateChannel(ip_port, grpc::InsecureChannelCredentials());
        stub_ = ServerMessages::ServerMessagesService::NewStub(channel);
    }

    [[nodiscard]] auto check_online() const noexcept -> hello_client::client_error
    {
        trace_span span(FILE_LINE_FUNC);
        grpc::ClientContext context;
        for (const auto &[key, value] : get_trace_headers()) {
            context.AddMetadata(key, value);
        }
        using namespace std::chrono_literals;
        constexpr auto k_check_online_timeout = 1000ms;
        context.set_deadline(std::chrono::system_clock::now() + k_check_online_timeout);

        const ServerMessages::CheckOnlineRequest request{};
        ServerMessages::CheckOnlineReply reply;

        const grpc::Status status = stub_->CheckOnline(&context, request, &reply);
        if (status.ok()) {
            spdlog::debug("check_online: server is online");
            return hello_client::client_error::K_OK;
        }

        spdlog::warn("check_online RPC failed: " + status.error_message());
        return hello_client::client_error::K_RPC_FAILED;
    }

private:
    std::unique_ptr<ServerMessages::ServerMessagesService::Stub> stub_;
};

// ---------------------------------------------------------------------------
// service_client 公共接口
// ---------------------------------------------------------------------------
service_client::service_client(const std::string &ip_port)
    : pimpl_(std::make_unique<Impl>(ip_port))
{
}

service_client::~service_client() = default;

auto service_client::check_online() const noexcept -> hello_client::client_error
{
    return pimpl_->check_online();
}

} // namespace client_messages