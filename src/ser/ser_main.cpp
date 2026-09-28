/// @file ser/ser_main.cpp
/// @brief 服务器入口。
///
/// 用法：
///   ./netdict_server [ip] [port]
///   ip    默认 0.0.0.0
///   port  默认 13140
///   Ctrl+C / SIGTERM 优雅停止

#include "ser/ser.hpp"
#include "common/logger.hpp"
#include "common/net.hpp"
#include "db/dict_repo.hpp"
#include "db/usr_repo.hpp"

#include <csignal>
#include <exception>    // std::exception 基类
#include <unistd.h>     // write

// 全局指针，供信号处理函数调 stop（signal handler 不能捕获复杂状态）
static Server* g_server = nullptr;

/// 信号处理：请求 Server 停止；write 一个换行（异步信号安全，让 ^C 后另起一行）。
static void onSignal(int) {
    if (g_server) g_server->stop();
    const char nl = '\n';
    write(STDERR_FILENO, &nl, 1);
}

int main(int argc, char* argv[]) {
    // 1. 解析参数（ip / port，均可缺省）
    std::string ip = (argc > 1) ? argv[1] : net::DEFAULT_IP;
    int port = (argc > 2) ? std::atoi(argv[2]) : net::DEFAULT_PORT;

    try {
        // 2. 打开数据库（不存在则建表）
        DictRepo dict("data/dict.db");
        UsrRepo  usr("data/usr.db");

        // 3. 导入词库（表非空则跳过）
        if (!dict.initFromFile("data/dict.txt")) {
            LOG_WARN("dict.txt not loaded (missing or already imported)");
        }

        // 4. 构造 Server 并注册信号
        Server server(dict, usr, ip, port);
        g_server = &server;
        std::signal(SIGINT,  onSignal);
        std::signal(SIGTERM, onSignal);

        // 5. 进入 accept 主循环（阻塞直到 stop）
        server.run();
    } catch (const std::exception& e) {
        LOG_ERR("fatal: %s", e.what());
        return 1;
    }
    return 0;
}