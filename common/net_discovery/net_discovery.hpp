#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace net_discovery
{

struct endpoint {
    std::string machine_id;
    std::string instance_name;
    std::string address;
    std::uint16_t port = 0;

    [[nodiscard]] auto ip_port() const -> std::string;
};

struct hello_server_config {
    std::string machine_id;
    std::uint16_t port = 50051;
    std::vector<std::string> interfaces;
    std::chrono::seconds announce_interval{ 30 };
};

// 当前项目只发现 hello_server，不对外暴露通用 DNS-SD browser。
// mDNS 自身固定使用 UDP 5353；port 表示客户端最终连接的 gRPC 业务端口。
class hello_server_announcer
{
public:
    explicit hello_server_announcer(hello_server_config config = {});
    ~hello_server_announcer();

    hello_server_announcer(const hello_server_announcer &) = delete;
    auto operator=(const hello_server_announcer &) -> hello_server_announcer & = delete;

    hello_server_announcer(hello_server_announcer &&) noexcept;
    auto operator=(hello_server_announcer &&) noexcept -> hello_server_announcer &;

    void start();
    void stop();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

auto local_machine_id() -> std::string;

auto discover_hello_servers(std::chrono::milliseconds timeout = std::chrono::milliseconds{ 1500 })
        -> std::vector<endpoint>;

auto discover_hello_server(
        const std::string &machine_id,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{ 1500 }
) -> std::optional<endpoint>;

} // namespace net_discovery
