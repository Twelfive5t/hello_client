/// @file src/messages/client_messages.cpp
/// @brief ServerMessagesService gRPC stub 封装实现

#include "messages/client_messages.hpp"

#include "server_messages.grpc.pb.h"
#include "telemetry/telemetry.hpp"
#include "update_service.grpc.pb.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <grpcpp/client_context.h>
#include <grpcpp/grpcpp.h>
#include <vector>

namespace client_messages
{
namespace
{

constexpr std::chrono::milliseconds K_RPC_TIMEOUT{ 1000 };
constexpr std::chrono::milliseconds K_UPDATE_TIMEOUT{ std::chrono::minutes{ 30 } };
constexpr std::size_t K_CHUNK_SIZE{ 64 * 1024 };

void setup_context(grpc::ClientContext &context, std::chrono::milliseconds timeout)
{
    for (const auto &[key, value] : get_trace_headers()) {
        context.AddMetadata(key, value);
    }
    context.set_deadline(std::chrono::system_clock::now() + timeout);
}

auto to_client_update_status(UpdateServiceMessages::UpdateStatus status
) -> hello_client::update_status
{
    switch (status) {
    case UpdateServiceMessages::IDLE:
        return hello_client::update_status::K_IDLE;
    case UpdateServiceMessages::UPLOADING:
        return hello_client::update_status::K_UPLOADING;
    case UpdateServiceMessages::VERIFYING:
        return hello_client::update_status::K_VERIFYING;
    case UpdateServiceMessages::INSTALLING:
        return hello_client::update_status::K_INSTALLING;
    case UpdateServiceMessages::STOPPING_BUSINESS:
        return hello_client::update_status::K_STOPPING_BUSINESS;
    case UpdateServiceMessages::STARTING_BUSINESS:
        return hello_client::update_status::K_STARTING_BUSINESS;
    case UpdateServiceMessages::HEALTH_CHECKING:
        return hello_client::update_status::K_HEALTH_CHECKING;
    case UpdateServiceMessages::SUCCEEDED:
        return hello_client::update_status::K_SUCCEEDED;
    case UpdateServiceMessages::ROLLING_BACK:
        return hello_client::update_status::K_ROLLING_BACK;
    case UpdateServiceMessages::ROLLED_BACK:
        return hello_client::update_status::K_ROLLED_BACK;
    case UpdateServiceMessages::FAILED:
        return hello_client::update_status::K_FAILED;
    }

    return hello_client::update_status::K_FAILED;
}

auto get_file_size(const std::string &package_path, std::uint64_t &file_size) -> bool
{
    std::ifstream input(package_path, std::ios::binary | std::ios::ate);
    if (!input) {
        return false;
    }

    const auto end_position = input.tellg();
    if (end_position <= 0) {
        return false;
    }

    file_size = static_cast<std::uint64_t>(end_position);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// PIMPL 实现 —— 持有 ServerMessagesService stub，所有 protobuf/grpc 细节封闭在此
// ---------------------------------------------------------------------------
class service_client::Impl
{
public:
    explicit Impl(const std::string &ip_port)
    {
        auto channel = create_grpc_client_channel_with_metrics(
                ip_port, grpc::InsecureChannelCredentials()
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

    [[nodiscard]] auto get_server_version(std::string &version
    ) const noexcept -> hello_client::client_error
    {
        trace_span span;
        grpc::ClientContext context;
        setup_context(context, K_RPC_TIMEOUT);

        const ServerMessages::GetServerVersionRequest request{};
        ServerMessages::GetServerVersionReply reply;
        const grpc::Status status = stub_->GetServerVersion(&context, request, &reply);

        if (status.ok()) {
            version = reply.version();
            spdlog::debug("get_server_version: {}", version);
            return hello_client::client_error::K_OK;
        }
        spdlog::warn("get_server_version RPC failed: " + status.error_message());
        return hello_client::client_error::K_RPC_FAILED;
    }

private:
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

auto service_client::get_server_version(std::string &version
) const noexcept -> hello_client::client_error
{
    return pimpl_->get_server_version(version);
}

// ---------------------------------------------------------------------------
// PIMPL 实现 —— 持有 UpdateService stub，所有 protobuf/grpc 细节封闭在此
// ---------------------------------------------------------------------------
class update_service_client::Impl
{
public:
    explicit Impl(const std::string &ip_port)
    {
        auto channel = create_grpc_client_channel_with_metrics(
                ip_port, grpc::InsecureChannelCredentials()
        );
        stub_ = UpdateServiceMessages::UpdateService::NewStub(channel);
    }

    [[nodiscard]] auto update_server(const std::string &package_path
    ) const noexcept -> hello_client::client_error
    {
        namespace fs = std::filesystem;

        try {
            std::uint64_t file_size{};
            if (!get_file_size(package_path, file_size)) {
                spdlog::warn("invalid update package: {}", package_path);
                return hello_client::client_error::K_RPC_FAILED;
            }

            std::ifstream input(package_path, std::ios::binary);
            if (!input) {
                spdlog::warn("failed to open update package: {}", package_path);
                return hello_client::client_error::K_RPC_FAILED;
            }

            trace_span span;
            grpc::ClientContext context;
            setup_context(context, K_UPDATE_TIMEOUT);

            UpdateServiceMessages::UpdateServerReply reply;
            auto writer = stub_->UpdateServer(&context, &reply);

            UpdateServiceMessages::UpdateServerRequest request;
            auto *info = request.mutable_info();
            info->set_file_name(fs::path(package_path).filename().string());
            info->set_file_size(static_cast<std::uint64_t>(file_size));
            if (!writer->Write(request)) {
                spdlog::warn("update_server failed to send package info");
                return hello_client::client_error::K_RPC_FAILED;
            }

            std::vector<char> buffer(K_CHUNK_SIZE);
            while (input) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto bytes_read = input.gcount();
                if (bytes_read <= 0) {
                    continue;
                }

                request.Clear();
                request.set_chunk(buffer.data(), static_cast<std::size_t>(bytes_read));
                if (!writer->Write(request)) {
                    spdlog::warn("update_server stream closed while uploading package");
                    return hello_client::client_error::K_RPC_FAILED;
                }
            }
            if (input.bad()) {
                spdlog::warn("failed while reading update package: {}", package_path);
                return hello_client::client_error::K_RPC_FAILED;
            }

            writer->WritesDone();
            const grpc::Status status = writer->Finish();
            if (status.ok() && reply.ok()) {
                spdlog::debug("update_server succeeded: {}", reply.message());
                return hello_client::client_error::K_OK;
            }

            spdlog::warn(
                    "update_server failed: grpc={}, reply={}",
                    status.error_message(),
                    reply.message()
            );
            return hello_client::client_error::K_RPC_FAILED;
        } catch (const std::exception &e) {
            spdlog::warn("update_server exception: {}", e.what());
            return hello_client::client_error::K_RPC_FAILED;
        } catch (...) {
            spdlog::warn("update_server unknown exception");
            return hello_client::client_error::K_RPC_FAILED;
        }
    }

    [[nodiscard]] auto get_update_status(hello_client::update_status_info &info
    ) const noexcept -> hello_client::client_error
    {
        trace_span span;
        grpc::ClientContext context;
        setup_context(context, K_RPC_TIMEOUT);

        const UpdateServiceMessages::GetUpdateStatusRequest request{};
        UpdateServiceMessages::GetUpdateStatusReply reply;
        const grpc::Status status = stub_->GetUpdateStatus(&context, request, &reply);

        if (status.ok()) {
            info.status = to_client_update_status(reply.status());
            info.progress_percent = reply.progress_percent();
            info.message = reply.message();
            return hello_client::client_error::K_OK;
        }
        spdlog::warn("get_update_status RPC failed: " + status.error_message());
        return hello_client::client_error::K_RPC_FAILED;
    }

private:
    std::unique_ptr<UpdateServiceMessages::UpdateService::Stub> stub_;
};

// ---------------------------------------------------------------------------
// update_service_client 公共接口
// ---------------------------------------------------------------------------
update_service_client::update_service_client(const std::string &ip_port)
    : pimpl_(std::make_unique<Impl>(ip_port))
{
}

update_service_client::~update_service_client() = default;

auto update_service_client::update_server(const std::string &package_path
) const noexcept -> hello_client::client_error
{
    return pimpl_->update_server(package_path);
}

auto update_service_client::get_update_status(hello_client::update_status_info &info
) const noexcept -> hello_client::client_error
{
    return pimpl_->get_update_status(info);
}

} // namespace client_messages
