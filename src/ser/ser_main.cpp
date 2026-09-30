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

#include <csignal>      // std::signal
#include <exception>    // std::exception 基类
#include <sys/stat.h>   // mkdir

// 全局指针，供信号处理函数调 requestStop()（signal handler 不能捕获复杂状态）
static Server* g_server = nullptr;

/// 信号处理：只调 requestStop()（只置标志）+ write（async-signal-safe），并补一个换行（让 ^C 后另起一行）。
//  为什么只用 g_server->requestStop() 和 write：
//   - signal handler 里"只能调'异步信号安全'函数"（如 write）；
//   - printf / fprintf / std::cout 不是（内部有锁 / 缓冲），可能死锁；
//   - write 是 POSIX 明确列出的异步信号安全函数。
static void onSignal(int) {
    if (g_server) g_server->requestStop();
    const char nl = '\n';
    write(STDERR_FILENO, &nl, 1);
}

int main(int argc, char* argv[]) {
    // 1. 解析参数（ip / port，均可缺省）
    std::string ip = (argc > 1) ? argv[1] : net::DEFAULT_IP;
    int port = (argc > 2) ? std::atoi(argv[2]) : net::DEFAULT_PORT;

    // 确保 logs 目录存在
    mkdir("logs", 0755);   // 存在则忽略（返回 -1 + EEXIST，正常）
    // 2. 日志走文件：避免日志（stderr）和终端 prompt（stdout）混在一起。
    //    打开失败不致命，退回 stderr（logger::file() 默认 stderr）。
    if (!logger::setFile("logs/netdict_server.log")) {
        LOG_WARN("open logs/netdict.log failed, fallback to stderr");
    }

    try {
        // 3. 打开数据库（不存在则建表）
        DictRepo dict("data/dict.db");
        UsrRepo  usr("data/usr.db");

        // 4. 导入词库（表非空则跳过）
        if (!dict.initFromFile("data/dict.txt")) {
            LOG_WARN("dict.txt not loaded (missing or already imported)");
        }

        // 5. 构造 Server 并注册信号
        Server server(dict, usr, ip, port);
        g_server = &server;
        std::signal(SIGINT,  onSignal);
        std::signal(SIGTERM, onSignal);

        // 6. 进入 epoll 主循环（阻塞直到 requestStop）
        server.run();
    } catch (const std::exception& e) {
        LOG_ERR("fatal: %s", e.what());
        return 1;
    }

    return 0;
}