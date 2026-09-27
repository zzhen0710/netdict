/// @file cli/cli.cpp
/// @brief 客户端实现：连接、发送、接收、显示。

#include "cli/cli.hpp"
#include "common/logger.hpp"

#include <arpa/inet.h>      // inet_addr
#include <netinet/in.h>     // sockaddr_in
#include <sys/socket.h>     // socket, connect, recv
#include <unistd.h>         // close, write

#include <cstring>          // memset
#include <iostream>
#include <stdexcept>        // std::runtime_error

/// 构造：创建 socket 并连接服务器。
Cli::Cli(std::string_view ip, int port)
    : sock_fd_(-1) {

    // 1. 建 socket
    sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ < 0) {
        throw std::runtime_error("socket failed");
    }

    // 2. 填服务器地址（sockaddr_in 用 {} 清零，避免 sin_zero 未初始化警告）
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr(std::string(ip).c_str());

    // 3. 连接；失败则关 fd 并抛
    if (connect(sock_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sock_fd_);
        throw std::runtime_error("connect failed");
    }

    LOG_INFO("connected to %.*s:%d",
             static_cast<int>(ip.size()), ip.data(), port);
}

/// 析构：关闭 socket。
Cli::~Cli() {
    if (sock_fd_ >= 0) {
        ::close(sock_fd_);
    }
}

/// 主循环：读 stdin → 发请求 → 收响应 → 显示。
void Cli::run() {
    std::string line;
    
    // 读一行 → 空行跳过 → 发送 → 收响应；EOF 退出
    while (readUsrLine(line)) {
        if (line.empty()) continue;
        if (!sendRequest(line)) continue;

        handleResp();
    }

    LOG_INFO("client exit");
}

/// 读一行用户输入；EOF 返回 false。
bool Cli::readUsrLine(std::string& line) {
    // 打印提示符并立即刷新
    std::cout << "> " << std::flush;
    // getline 返回流状态，转 bool：成功 true，EOF false
    return static_cast<bool>(std::getline(std::cin, line));
}

/// 处理并发送用户输入：去前导 "."；空行返回 false。
bool Cli::sendRequest(std::string_view usr_req) {
    // 去前导 "."
    if (!usr_req.empty() && usr_req.front() == '.') {
        usr_req.remove_prefix(1);
    }

    // 空行忽略
    if (usr_req.empty()) return false;

    // 拼 "cmd args\n"
    std::string out(usr_req);
    out += '\n';

    // 发
    if (net::sendAll(sock_fd_, out.data(), out.size()) < 0) {
        LOG_ERR("send failed");
        return false;
    }

    return true;
}

/// 收一行响应；对端关闭 / 出错返回空串。
std::string Cli::recvLine() {

}

/// 处理响应：解析 ok / err；ok <n> 时再读 n 行并显示。
void Cli::handleResp() {

}

/// 打印一行。
void Cli::printLine(std::string_view s) {
    // 服务器按长度发送（可能不含 '\0'），这里同样显式按长度写出，
    // 不依赖 '\0'，避免越界或截断。
    std::cout.write(s.data(), static_cast<std::streamsize>(s.size()));
    std::cout << '\n';
}