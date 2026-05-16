/// @file src/messages/client_messages.cpp
/// @brief ServerMessagesService gRPC stub 封装实现

#include "messages/client_messages.hpp"

#include "server_messages.grpc.pb.h"
#include "telemetry/telemetry.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/grpcpp.h>

namespace client_messages
{

// ---------------------------------------------------------------------------
// PIMPL 实现 —— 持有 gRPC stub，所有 protobuf/grpc 细节封闭在此
// ---------------------------------------------------------------------------
class service_client::Impl
{
public:
    explicit Impl(const std::string &ip_port)
    {
        std::vector<std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>>
                interceptors;
        install_grpc_client_metrics(interceptors);
        auto channel = grpc::experimental::CreateCustomChannelWithInterceptors(
                ip_port,
                grpc::InsecureChannelCredentials(),
                grpc::ChannelArguments{},
                std::move(interceptors)
        );
        stub_ = ServerMessages::ServerMessagesService::NewStub(channel);
    }

    [[nodiscard]] auto check_online() const noexcept -> hello_client::client_error
    {
        trace_span span;
        grpc::ClientContext context;
        setup_context(context, K_RPC_TIMEOUT);

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

    [[nodiscard]] auto exit_server() const noexcept -> hello_client::client_error
    {
        trace_span span;
        grpc::ClientContext context;
        setup_context(context, K_RPC_TIMEOUT);

        const ServerMessages::ExitServerRequest request{};
        ServerMessages::ExitServerReply reply;
        const grpc::Status status = stub_->ExitServer(&context, request, &reply);

        if (status.ok()) {
            spdlog::debug("exit_server: server acknowledged exit request");
            return hello_client::client_error::K_OK;
        }
        spdlog::warn("exit_server RPC failed: " + status.error_message());
        return hello_client::client_error::K_RPC_FAILED;
    }

private:
    static void setup_context(grpc::ClientContext &context, std::chrono::milliseconds timeout)
    {
        for (const auto &[key, value] : get_trace_headers()) {
            context.AddMetadata(key, value);
        }
        context.set_deadline(std::chrono::system_clock::now() + timeout);
    }

    static constexpr std::chrono::milliseconds K_RPC_TIMEOUT{ 1000 };
    std::unique_ptr<ServerMessages::ServerMessagesService::Stub> stub_;
};

// ---------------------------------------------------------------------------
// service_client 公共接口
// ---------------------------------------------------------------------------
service_client::service_client(const std::string &ip_port) : pimpl_(std::make_unique<Impl>(ip_port))
{
}

service_client::~service_client() = default;

auto service_client::check_online() const noexcept -> hello_client::client_error
{
    return pimpl_->check_online();
}

auto service_client::exit_server() const noexcept -> hello_client::client_error
{
    return pimpl_->exit_server();
}

} // namespace client_messages
