/// @file src/client/src/hello_client.cpp
/// @brief hello_client 门面类 PIMPL 实现

#include "hello_client.hpp"

#include "telemetry/telemetry.hpp"

#include <spdlog/spdlog.h>

namespace hello_client
{
namespace
{

auto make_otlp_endpoint(const std::string &ip_port) -> std::string
{
    const auto colon = ip_port.rfind(':');
    std::string host = (colon != std::string::npos) ? ip_port.substr(0, colon) : ip_port;
    if (host == "0.0.0.0") {
        host = "127.0.0.1";
    }
    return host + ":4317";
}

auto make_update_endpoint(const std::string &ip_port) -> std::string
{
    const auto colon = ip_port.rfind(':');
    std::string host = (colon != std::string::npos) ? ip_port.substr(0, colon) : ip_port;
    if (host == "0.0.0.0") {
        host = "127.0.0.1";
    }
    return host + ":50052";
}

} // namespace

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

auto hello_client::create(const std::string &ip_port) noexcept -> client_error
{
    if (ip_port.empty()) {
        spdlog::error("hello_client::create requires an explicit ip:port");
        return client_error::K_CONNECTION_FAILED;
    }

    init_tracer({ .service_name = "hello_client", .endpoint = make_otlp_endpoint(ip_port) });
    const auto err = client.init(ip_port);
    if (err != client_error::K_OK) {
        spdlog::error("hello_client::create failed to connect to " + ip_port);
        return err;
    }

    const auto update_endpoint = make_update_endpoint(ip_port);
    const auto update_err = update.init(update_endpoint);
    if (update_err != client_error::K_OK) {
        spdlog::error("hello_client::create failed to connect to " + update_endpoint);
    }
    return update_err;
}

} // namespace hello_client
