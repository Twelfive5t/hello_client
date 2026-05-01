#include "server_messages.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

namespace
{

void run_client()
{
    const std::string SERVER_ADDRESS = "localhost:50051";

    // 1. 创建 channel
    auto channel = grpc::CreateChannel(SERVER_ADDRESS, grpc::InsecureChannelCredentials());

    // 2. 创建 stub
    auto stub = ServerMessages::ServerMessagesService::NewStub(channel);

    // 3. 发起 RPC 调用
    grpc::ClientContext context;
    const ServerMessages::CheckOnlineRequest REQUEST{};
    ServerMessages::CheckOnlineReply reply;

    const grpc::Status STATUS = stub->CheckOnline(&context, REQUEST, &reply);

    if (STATUS.ok()) {
        spdlog::info("Server is online");
    } else {
        spdlog::error("RPC failed: {}", STATUS.error_message());
    }
}

} // namespace

auto main(int /*argc*/, char * /*argv*/[]) -> int
{
    spdlog::info("Hello, Client!");
    run_client();
    return 0;
}
