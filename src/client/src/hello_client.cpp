/// @file src/client/src/hello_client.cpp
/// @brief hello_client 门面类 PIMPL 实现

#include "hello_client.hpp"

#include "telemetry/telemetry.hpp"

#include <spdlog/spdlog.h>

namespace hello_client
{

// ---------------------------------------------------------------------------
// PIMPL —— 目前无私有状态，预留扩展位置（如全局 channel、认证令牌等）
// ---------------------------------------------------------------------------
class hello_client::Impl
{
public:
    Impl() = default;
};

// ---------------------------------------------------------------------------
// hello_client 公共接口
// ---------------------------------------------------------------------------
hello_client::hello_client() : pimpl_(std::make_unique<Impl>())
{
}

hello_client::~hello_client()
{
    cleanup_tracer();
}

auto hello_client::create(const std::string &ip_port, const std::string &jaeger_endpoint) noexcept
        -> client_error
{
    if (ip_port.empty()) {
        spdlog::error("hello_client::create requires an explicit ip:port");
        return client_error::K_CONNECTION_FAILED;
    }

    // 如果未显式指定 Jaeger 地址，自动从 ip_port 中提取 IP，拼接端口 4317
    // 0.0.0.0 是绑定地址，不能作为连接目标，自动回退到 127.0.0.1
    std::string effective_jaeger = jaeger_endpoint;
    if (effective_jaeger.empty()) {
        const auto colon = ip_port.rfind(':');
        std::string host = (colon != std::string::npos) ? ip_port.substr(0, colon) : ip_port;
        if (host == "0.0.0.0") {
            host = "127.0.0.1";
        }
        effective_jaeger = host + ":4317";
    }
    init_tracer({ .service_name = "hello_client", .endpoint = effective_jaeger });
    const auto err = client.init(ip_port);
    if (err != client_error::K_OK) {
        spdlog::error("hello_client::create failed to connect to " + ip_port);
    }
    return err;
}

} // namespace hello_client
