/// @file common/net.cpp
/// @brief 网络 IO 工具实现。

#include "common/net.hpp"

#include <sys/socket.h>   // send, recv

namespace net {

    ssize_t sendAll(int fd, const char* buf, std::size_t len) {
        std::size_t sent = 0;

        while (sent < len) {
            // TCP 不保证一次发完：每次只发"剩余部分"，累加已发字节
            ssize_t n = send(fd, buf + sent, len - sent, 0);
            if (n <= 0) return -1;   // 出错 / 对端关闭
            sent += static_cast<std::size_t>(n);
        }

        return static_cast<ssize_t>(len);
    }

    ssize_t recvAll(int fd, char* buf, std::size_t len) {
        std::size_t recvd = 0;

        while (recvd < len) {
            // TCP 不保证一次收全：每次只收"剩余部分"，累加已收字节
            ssize_t n = recv(fd, buf + recvd, len - recvd, 0);
            if (n <= 0) return -1;
            recvd += static_cast<std::size_t>(n);
        }
        
        return static_cast<ssize_t>(len);
    }

    /// 收一行：缓冲无 '\n' 时 recv 新数据，攒到 '\n' 切出一行。
    bool recvLine(int fd, std::string& recv_buf, std::string& line) {
        while (true) {
            // 缓冲里已有 '\n'：切出一行，剩余留缓冲
            auto pos = recv_buf.find('\n');
            if (pos != std::string::npos) {
                line = recv_buf.substr(0, pos);
                recv_buf.erase(0, pos + 1);
                return true;
            }
            // 否则 recv 一次多字节，追加到缓冲
            char buf[BUF_SIZE];
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) return false;
            recv_buf.append(buf, static_cast<std::size_t>(n));
        }
    }

}   // namespace net