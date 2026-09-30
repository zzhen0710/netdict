/// @file cli/cli_main.cpp
/// @brief 客户端入口。
///
/// 用法：
///   ./netdict_client [ip] [port]
///   ip    默认 0.0.0.0（同机测试用 127.0.0.1）
///   port  默认 13140
///   输入以 "." 开头（如 .query apple）；Ctrl+D / Ctrl+C (Ctrl+C 还需 +'\n') 退出

#include "cli/cli.hpp"
#include "common/net.hpp"
#include "common/logger.hpp"

#include <csignal>      // std::signal
#include <exception>    // std::exception 基类
#include <iostream>
#include <unistd.h>     // std::write
#include <sys/stat.h>   // mkdir

// 全局指针：signal handler 只能操作简单状态
static Cli* g_cli = nullptr;

/// 信号处理：请求 Cli 停止，并补一个换行。
//  只调 stop() 和 write：signal handler 里只能调'异步信号安全'函数
//  （write 是；printf / std::cout 不是，可能死锁）。
static void onSignal(int) {
    if (g_cli) g_cli->stop();
    const char nl = '\n';
    write(STDERR_FILENO, &nl, 1);
}

int main(int argc, char* argv[]) {
    // 1. 解析参数（ip / port，均可缺省）
    std::string ip = (argc > 1) ? argv[1] : net::DEFAULT_IP;
    int port = (argc > 2) ? std::atoi(argv[2]) : net::DEFAULT_PORT;

    // 2. 日志走文件：避免日志（stderr）和 prompt（stdout）混。
    mkdir("logs", 0755);
    if (!logger::setFile("logs/netdict_client.log")) {
        // 打开失败：退回 stderr（logger::file() 默认 stderr）
        LOG_WARN("open logs/netdict_client.log failed, fallback to stderr");
    }

    try {
        // 3. 构造 Client 连接服务器并注册信号
        Cli cli(ip, port);
        g_cli = &cli;
        std::signal(SIGINT,  onSignal);     
        std::signal(SIGTERM, onSignal);    

        // 4. 进入"读 → 发 → 收 → 显示"循环
        cli.run();
    } catch (const std::exception& e) {
        std::cerr << "client: " << e.what() << '\n';
        return 1;
    }

    return 0;
}