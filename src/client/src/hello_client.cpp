/// @file src/client/src/hello_client.cpp
/// @brief hello_client 门面类 PIMPL 实现

#include "hello_client.hpp"

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

hello_client::~hello_client() = default;

auto hello_client::create(const std::string &ip_port) noexcept -> client_error
{
    const auto err = client.init(ip_port);
    if (err != client_error::K_OK) {
        spdlog::error("hello_client::create failed to connect to " + ip_port);
    }
    return err;
}

} // namespace hello_client