/// @file ser/server.cpp
/// @brief 服务器实现（第一版：accept + 收一行 + 回固定串）。

#include "ser/ser.hpp"
#include "common/logger.hpp"
#include "common/net.hpp"      // net::BUF_SIZE（← 你漏了）

#include <arpa/inet.h>      // inet_addr, inet_ntoa
#include <netinet/in.h>     // sockaddr_in
#include <sys/socket.h>     // socket, bind, listen, accept, recv, send
#include <unistd.h>         // close

#include <cstring>          // strlen
#include <stdexcept>        // std::runtime_error
#include <string_view>

/// 构造：创建 socket、bind、listen。
Server::Server(std::string_view ip, int port)
    : listen_fd_(-1), ip_(ip), port_(port), running_(false) {

    // 1. 创建 TCP 套接字
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error("socket failed");
    }

    // 2. 端口复用：避免 TIME_WAIT 期间无法立即重启
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. 构造本机地址
    sockaddr_in addr {
        AF_INET,
        htons(static_cast<uint16_t>(port_)),
        { inet_addr(ip_.c_str()) },
        {}                   // 空，等价于全 0
    };

    // 4. bind
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error("bind failed");
    }

    // 5. listen
    if (listen(listen_fd_, 128) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error("listen failed");
    }

    LOG_INFO("server listening on %s:%d", ip_.c_str(), port_);
}

/// 析构：关闭监听套接字。
Server::~Server() {
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
    }
}

/// 启动：阻塞 accept 循环。
void Server::run() {
    running_ = true;

    while (running_) {
        sockaddr_in cli{};
        socklen_t   cli_len = sizeof(cli);

        // 收一个连接
        int cfd = accept(listen_fd_,
                         reinterpret_cast<sockaddr*>(&cli), &cli_len);
        if (cfd < 0) {
            if (running_) LOG_ERR("accept failed");
            continue;
        }

        LOG_INFO("client connected: %s:%d",
                 inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));

        // 单线程：处理完再 accept 下一个
        handleClient(cfd);
        ::close(cfd);

        LOG_INFO("client disconnected");
    }
}

/// 请求停止（幂等）。
void Server::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

/// 处理单个客户端：收一行 → 回一行 → 返回。
void Server::handleClient(int cfd) {
    char buf[net::BUF_SIZE];

    // 收一行（第一版：只收一次，不做"攒到 \n"）
    // 用 sizeof(buf) 而非 sizeof(buf)-1：string_view 不依赖 \0，不必留位
    ssize_t n = recv(cfd, buf, sizeof(buf), 0);
    if (n <= 0) {
        LOG_WARN("recv failed or peer closed");
        return;
    }

    // string_view 指向 buf，按长度定界（不靠 \0）
    std::string_view line(buf, static_cast<size_t>(n));

    // 协议：一行不含 \n
    if (!line.empty() && line.back() == '\n') {
        line.remove_suffix(1);
    }

    LOG_INFO("recv: %.*s", static_cast<int>(line.size()), line.data()); // 带精度打印，按长度不按'\0'

    // 第一版：固定回复
    const char* resp = "world\n";
    ssize_t sent = send(cfd, resp, std::strlen(resp), 0);
    if (sent < 0) {
        LOG_ERR("send failed");
    }
}