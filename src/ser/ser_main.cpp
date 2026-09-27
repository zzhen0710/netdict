/// @file ser/ser_main.cpp
/// @brief 服务器入口：解析参数 → 打开数据库 → 构造 Server → run。

#include "ser/ser.hpp"
#include "common/logger.hpp"

#include <csignal>
#include <cstdlib>      // std::atoi
#include <exception>
#include <string>
#include <unistd.h>     // write

// 全局指针，供信号处理函数调 stop（signal handler 不能捕获复杂状态）
static Server* g_server = nullptr;

/// 信号处理：请求 Server 停止。
static void onSignal(int) {
    if (g_server) { g_server->stop(); }
    const char nl = '\n';
    write(STDERR_FILENO, &nl, 1);   // 终端写入换行，让"^C"后另起一行，write 异步信号安全
}

int main(int argc, char* argv[]) {
    std::string ip = (argc > 1) ? argv[1] : net::DEFAULT_IP;
    int port = (argc > 2) ? std::atoi(argv[2]) : net::DEFAULT_PORT;

    try {
        // 打开数据库（文件不存在时自动建表）
        DictRepo dict("data/dict.db");
        UsrRepo  usr("data/usr.db");

        // 导入词库（表非空则跳过）
        if (!dict.initFromFile("data/dict.txt")) {
            LOG_WARN("dict.txt not loaded (missing or already imported)");
        }

        Server server(dict, usr, ip, port);
        g_server = &server;

        std::signal(SIGINT,  onSignal);
        std::signal(SIGTERM, onSignal);

        server.run();
    } catch (const std::exception& e) {
        LOG_ERR("fatal: %s", e.what());
        return 1;
    }
    return 0;
}