/// @file test/client/client_test.cpp
/// @brief hello_client 接口测试 —— CheckOnline

#include <gtest/gtest.h>
#include "hello_client.hpp"

extern const char *IP_PORT; // defined in src/main_test.cpp

class HelloClientTest : public ::testing::Test {};

/// @brief 向服务器发送 CheckOnline 请求，验证服务端正常响应
TEST_F(HelloClientTest, CheckOnline) {
    hello_client::hello_client client;
    ASSERT_EQ(client.create(IP_PORT), hello_client::client_error::K_OK)
        << "Failed to connect to " << IP_PORT;
    ASSERT_EQ(client.client.check_online(), hello_client::client_error::K_OK)
        << "Server is not online at " << IP_PORT;
}
