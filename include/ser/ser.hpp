/// @file ser/server.hpp
/// @brief 服务器主类：监听、收连接、处理请求（单线程骨架版）。

#pragma once

#include "common/net.hpp"
#include <string>
#include <string_view>

/// TCP 服务器：单线程 accept 循环，处理完一个客户端再收下一个。
/// 第一版只做"收一行 + 回固定串"；后续再接入 proto / repo。
class Server {
public:
    /// 构造：创建 socket、bind、listen。
    /// @param ip    监听 IP（"0.0.0.0" = 所有网卡）
    /// @param port  监听端口
    /// @throws std::runtime_error socket / bind / listen 失败
    Server(std::string_view ip = net::DEFAULT_IP,
           int port = net::DEFAULT_PORT);

    /// 析构：关闭监听套接字。
    ~Server();

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    /// 启动：阻塞 accept 循环，直到 stop()。
    void run();

    /// 请求停止（可从信号处理调）。
    void stop();

private:
    /// 处理单个客户端连接：收一行 → 回一行。
    void handleClient(int cfd);

    int listen_fd_;           ///< 监听套接字
    std::string ip_;          ///< 监听 IP（string_view 转存）
    int port_;                ///< 监听端口
    bool running_;            ///< 运行标志
};