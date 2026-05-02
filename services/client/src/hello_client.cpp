#include "server_messages.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

namespace
{

void run_client()
{
    const std::string server_address = "172.19.15.204:50051";

    // 1. 创建 channel
    auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());

    // 2. 创建 stub
    auto stub = ServerMessages::ServerMessagesService::NewStub(channel);

    // 3. 发起 RPC 调用
    grpc::ClientContext context;
    const ServerMessages::CheckOnlineRequest request{};
    ServerMessages::CheckOnlineReply reply;

    const grpc::Status status = stub->CheckOnline(&context, request, &reply);

    if (status.ok()) {
        spdlog::info("Server is online");
    } else {
        spdlog::error("RPC failed: {}", status.error_message());
    }
}

} // namespace

auto main(int /*argc*/, char * /*argv*/[]) -> int
{
    spdlog::info("Hello, Client!");
    run_client();
    return 0;
}
