/// @file ser/ser.cpp
/// @brief 服务器实现（第一版：accept + 收一行 + 解码分发 + 回一行）。

#include "ser/ser.hpp"
#include "common/logger.hpp"
#include "common/net.hpp"
#include "common/utils.hpp"     // utils::overloaded

#include <arpa/inet.h>      // inet_addr, inet_ntoa
#include <netinet/in.h>     // sockaddr_in
#include <sys/socket.h>     // socket, bind, listen, accept, recv
#include <unistd.h>         // close

#include <stdexcept>        // std::runtime_error
#include <string>
#include <string_view>
#include <variant>          // std::visit

// ---- 构造 / 析构 ----

/// 构造：创建 socket、bind、listen。
Server::Server(DictRepo& dict, UsrRepo& usr,
               std::string_view ip, int port)
    : dict_(dict), usr_(usr),
      thread_pool_(4), listen_fd_(-1),
      ip_(ip), port_(port), running_(false) {   // 列表初始化顺序要和成员变量声明顺序一致

    // 1. 创建 TCP 套接字
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error("socket failed");
    }

    // 2. 端口复用：避免 TIME_WAIT 期间无法立即重启
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. 构造本机地址（sockaddr_in 用 {} 清零，避免 sin_zero 未初始化警告）
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    addr.sin_addr.s_addr = inet_addr(ip_.c_str());

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

// ---- 运行 / 停止 ----

/// 启动：阻塞 accept 循环。
void Server::run() {
    running_ = true;                    // 置运行标志，进入主循环

    // 主循环：只要 running_ 为真，就一直收连接
    while (running_) {
        sockaddr_in cli{};              // 存客户端地址（{} 清零）
        socklen_t   cli_len = sizeof(cli);

        // 1. 阻塞收一个连接；失败（如被 stop 唤醒）则跳过
        int cfd = accept(listen_fd_,
                         reinterpret_cast<sockaddr*>(&cli), &cli_len);
        if (cfd < 0) {
            if (running_) LOG_ERR("accept failed");
            continue;
        }

        // 2. 记录客户端地址（inet_ntoa 转 IP，ntohs 转端口）
        LOG_INFO("client connected: %s:%d (fd = %d)",
                inet_ntoa(cli.sin_addr), ntohs(cli.sin_port), cfd);

        // 3. 把连接交给线程池处理：handleClient → 清会话 → 关 fd
        thread_pool_.addTask([this, cfd] {
            handleClient(cfd);              // 处理该连接的请求

            // 先取用户名（sessionErase 前），供 disconnected 日志用
            // 只为 disconnected 日志取 usr；Info 关时不取（零开销）
            if (logger::enabled(logger::Level::Info)) {
                auto name = sessionGet(cfd);
                const std::string usr = name ? *name : "-";
                LOG_INFO("client disconnected: fd = %d, usr = %s", cfd, usr.c_str());
            }

            // 连接关闭，清会话（handleClient 外部，防止多出口）
            sessionErase(cfd);              // 加锁清该连接的会话
            ::close(cfd);                   // 关连接
        });
    }
}

/// 请求停止（幂等）。
void Server::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    thread_pool_.stop();    // ← 主动等池子
}

//  ---- 单个客户端处理 ----

/// 处理单个客户端：收一行 → decodeUsr → visit 分发 → 回一行。
void Server::handleClient(int cfd) {
    recv_buf_.clear();   // 进入新连接，清上次残留

    std::string line;
    // 启动循环服务器
    while (true) {
        // 收一行（net::recvLine：从 recv_buf_ 切；不足时再 recv）
        if (!net::recvLine(cfd, recv_buf_, line)) {
            // 取用户名，供日志
            if (logger::enabled(logger::Level::Info)) {
                auto name = sessionGet(cfd);
                LOG_INFO("peer closed: fd = %d, usr = %s",
                        cfd, name ? name->c_str() : "-");
            }
            break;
        }

        if (logger::enabled(logger::Level::Info)) {
            auto name = sessionGet(cfd);
            const char* who = name ? name->c_str() : "-";
            LOG_INFO("recv: fd = %d, usr = %s, data = %.*s",
                    cfd, who, static_cast<int>(line.size()), line.data());
        }

        // 1. 解析
        auto msg = proto::decodeUsr(line);
        if (!msg) {
            sendLine(cfd, proto::makeErr("err", "bad request"));
            continue;
        }

        // 2. 分发（四类命令；SysCmd 不允许客户端发）
        std::visit(utils::overloaded {
            [&](proto::UsrCmd::Dict c) { handleUsrDict(cfd, *msg, c); },
            [&](proto::UsrCmd::Ctrl c) { handleUsrCtrl(cfd, *msg, c); },
            [&](proto::SysCmd::Dict)   { sendLine(cfd, proto::makeErr("err", "forbidden")); },
            [&](proto::SysCmd::Ctrl)   { sendLine(cfd, proto::makeErr("err", "forbidden")); },
        }, msg->cmd);
    }
}

// ---- 临界区会话管理 ----

/// 查该 cfd 的用户名；未登录返回 nullopt
std::optional<std::string> Server::sessionGet(int cfd) {
    // 加锁：sessions_ 可能被多线程（accept、handleClient）访问
    std::lock_guard<std::mutex> lk(sessions_mtx_);

    // 查 cfd；不在则未登录
    auto it = sessions_.find(cfd);
    if (it == sessions_.end()) {
        LOG_DEBUG("sessionGet: fd = %d not found", cfd);
        return std::nullopt;
    }
    LOG_DEBUG("sessionGet: fd = %d usr = %s", cfd, it->second.c_str());

    return it->second;
}

/// 登录 / 注册成功：记 cfd → name
void Server::sessionSet(int cfd, const std::string& name) {
    // 加锁：写入 sessions_
    std::lock_guard<std::mutex> lk(sessions_mtx_);
    // 覆盖 / 新建该 cfd 的会话
    sessions_[cfd] = name;

    LOG_DEBUG("sessionSet: fd = %d usr = %s", cfd, name.c_str());
}

/// 登出 / 连接关：清 cfd 会话
void Server::sessionErase(int cfd) {
    // 加锁：删除 sessions_ 条目
    std::lock_guard<std::mutex> lk(sessions_mtx_);
    sessions_.erase(cfd);

    LOG_DEBUG("sessionErase: fd = %d", cfd);
}

// ---- 发送辅助 ----

/// 发送原始字节（不补 \n）；net::sendAll 保证发完整。
void Server::sendBytes(int cfd, std::string_view data) {
    if (net::sendAll(cfd, data.data(), data.size()) < 0) {
        LOG_ERR("send failed");
    }
}

/// 发送一行（自动补 \n）；行协议专用。
void Server::sendLine(int cfd, std::string_view line) {
    // 拼出 "line\n"；std::string 拥有内容，传给 sendAll
    std::string out(line);
    out += '\n';

    if (logger::enabled(logger::Level::Debug)) {
        auto name = sessionGet(cfd);
        const char* who = name ? name->c_str() : "-";
        LOG_DEBUG("send: fd = %d, usr = %s, data = %.*s",
                cfd, who, static_cast<int>(line.size()), line.data());
    }
    
    sendBytes(cfd, out);
}