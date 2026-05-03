/// @file src/client/src/hello_client.cpp
/// @brief hello_client 门面类 PIMPL 实现

#include "hello_client.hpp"
#include "telemetry/telemetry.hpp"

#include <spdlog/spdlog.h>

namespace hello_client {

// ---------------------------------------------------------------------------
// PIMPL —— 目前无私有状态，预留扩展位置（如全局 channel、认证令牌等）
// ---------------------------------------------------------------------------
class hello_client::Impl {
public:
    Impl() = default;
};

// ---------------------------------------------------------------------------
// hello_client 公共接口
// ---------------------------------------------------------------------------
hello_client::hello_client() : pimpl_(std::make_unique<Impl>()) {}

hello_client::~hello_client()
{
    cleanup_tracer();
}

auto hello_client::create(
    const std::string &ip_port,
    const std::string &jaeger_endpoint
) noexcept -> client_error
{
// 如果未显式指定 Jaeger 地址，自动从 ip_port 中提取 IP，拼接端口 4317
std::string effective_jaeger = jaeger_endpoint;
if (effective_jaeger.empty()) {
    const auto colon = ip_port.rfind(':');
    const std::string host = (colon != std::string::npos) ? ip_port.substr(0, colon) : ip_port;
    effective_jaeger = host + ":4317";
}
init_tracer({.service_name = "hello_client", .endpoint = effective_jaeger});
const auto err = client.init(ip_port);
if (err != client_error::K_OK) {
    spdlog::error("hello_client::create failed to connect to " + ip_port);
}
return err;
}

} // namespace hello_client