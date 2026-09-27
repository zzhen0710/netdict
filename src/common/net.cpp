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

}   // namespace net