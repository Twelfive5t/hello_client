/// @file test/client/client_test.cpp
/// @brief hello_client 接口测试 —— CheckOnline

#include <gtest/gtest.h>
#include "hello_client.hpp"
#include "net_discovery/net_discovery.hpp"

#include <iostream>

extern const char *IP_PORT; // defined in src/main_test.cpp
extern const char *UPDATE_PACKAGE_PATH; // defined in src/main_test.cpp

class HelloClientTest : public ::testing::Test {};

/// @brief SDK 初始化不做隐式 mDNS 发现；调用方应先发现并选择 ip:port。
TEST_F(HelloClientTest, CreateRequiresExplicitAddress) {
    hello_client::hello_client client;
    ASSERT_EQ(client.create(""), hello_client::client_error::K_CONNECTION_FAILED);
}

/// @brief 打印通过 mDNS 发现到的所有 hello_server 实例；无发现结果时不判失败。
TEST_F(HelloClientTest, DiscoverHelloServers) {
    const auto endpoints = net_discovery::discover_hello_servers();
    std::cout << "Discovered hello_server count: " << endpoints.size() << "\n";
    for (const auto &endpoint : endpoints) {
        std::cout << "  machine_id=" << endpoint.machine_id
                  << ", ip_port=" << endpoint.ip_port()
                  << ", instance=" << endpoint.instance_name << "\n";
        EXPECT_FALSE(endpoint.machine_id.empty());
        EXPECT_FALSE(endpoint.ip_port().empty());
    }
}

/// @brief 向服务器发送 CheckOnline 请求，验证服务端正常响应
TEST_F(HelloClientTest, CheckOnline) {
    hello_client::hello_client client;
    ASSERT_EQ(client.create(IP_PORT), hello_client::client_error::K_OK)
        << "Failed to connect to " << IP_PORT;
    ASSERT_EQ(client.client.check_online(), hello_client::client_error::K_OK)
        << "Server is not online at " << IP_PORT;
}

/// @brief 查询服务器 OTA 升级状态，验证 GetUpdateStatus 接口正常响应
TEST_F(HelloClientTest, GetUpdateStatus) {
    hello_client::hello_client client;
    ASSERT_EQ(client.create(IP_PORT), hello_client::client_error::K_OK)
        << "Failed to connect to " << IP_PORT;

    hello_client::update_status_info info;
    ASSERT_EQ(client.update.get_update_status(info), hello_client::client_error::K_OK)
        << "Failed to get update status from " << IP_PORT;
    EXPECT_LE(info.progress_percent, 100U);
}

/// @brief 上传 OTA 升级包，验证 UpdateServer 接口正常响应
TEST_F(HelloClientTest, UpdateServer) {
    hello_client::hello_client client;
    ASSERT_EQ(client.create(IP_PORT), hello_client::client_error::K_OK)
        << "Failed to connect to " << IP_PORT;
    ASSERT_EQ(client.update.update_server(UPDATE_PACKAGE_PATH), hello_client::client_error::K_OK)
        << "Failed to update server at " << IP_PORT
        << " with package " << UPDATE_PACKAGE_PATH;
}

/// @brief 向服务器发送 ExitServer 请求，验证服务端正常响应退出指令
TEST_F(HelloClientTest, ExitServer) {
    hello_client::hello_client client;
    ASSERT_EQ(client.create(IP_PORT), hello_client::client_error::K_OK)
        << "Failed to connect to " << IP_PORT;
    ASSERT_EQ(client.client.exit_server(), hello_client::client_error::K_OK)
        << "Server did not acknowledge exit request at " << IP_PORT;
}
