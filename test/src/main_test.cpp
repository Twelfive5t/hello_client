/// @file test/src/main_test.cpp
/// @brief 测试程序入口
///
/// 通过 --server=ip:port 或环境变量 INTEGRATION_TEST_SERVER 指定服务器地址。

#include <gtest/gtest.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

#include "client/client_test.cpp" // NOLINT(bugprone-suspicious-include)

const char *IP_PORT = "127.0.0.1:50051"; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

auto main(int argc, char *argv[]) -> int { // NOLINT(bugprone-exception-escape)
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (const auto *addr = std::getenv("INTEGRATION_TEST_SERVER")) {
        IP_PORT = addr;
        std::cout << "Server: " << IP_PORT << "\n";
    }
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]); // NOLINT
        constexpr std::string_view k_prefix = "--server=";
        if (arg.starts_with(k_prefix)) {
            static std::string buf(arg.substr(k_prefix.size()));
            IP_PORT = buf.c_str();
            std::cout << "Server (argv): " << IP_PORT << "\n";
        }
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
