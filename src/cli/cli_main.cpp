/// @file cli/cli_main.cpp
/// @brief 客户端入口。
///
/// 用法：
///   ./netdict_client [ip] [port]
///   ip    默认 0.0.0.0（同机测试用 127.0.0.1）
///   port  默认 13140
///   输入以 "." 开头（如 .query apple）；Ctrl+D 退出

#include "cli/cli.hpp"
#include "common/net.hpp"

#include <exception>    // std::exception 基类
#include <iostream>

int main(int argc, char* argv[]) {
    // 1. 解析参数（ip / port，均可缺省）
    std::string ip = (argc > 1) ? argv[1] : net::DEFAULT_IP;
    int port = (argc > 2) ? std::atoi(argv[2]) : net::DEFAULT_PORT;

    try {
        // 2. 连接服务器
        Cli cli(ip, port);

        // 3. 进入"读 → 发 → 收 → 显示"循环
        cli.run();
    } catch (const std::exception& e) {
        std::cerr << "client: " << e.what() << '\n';
        return 1;
    }
    return 0;
}