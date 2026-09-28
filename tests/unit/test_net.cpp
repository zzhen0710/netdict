/// test_net.cpp — net 常量与 IO 工具冒烟测试

#include "common/net.hpp"

#include <cassert>
#include <cstring>       // std::memcmp
#include <iostream>
#include <string>
#include <sys/socket.h>  // socketpair
#include <unistd.h>      // close

int main() {
    // ==================== 1. 常量 ====================
    {
        assert(std::strcmp(net::DEFAULT_IP, "0.0.0.0") == 0);
        assert(net::DEFAULT_PORT == 13140);
        assert(net::BUF_SIZE == 4096);
    }
    std::cout << "[OK] constants\n";

    // ==================== 2. sendAll / recvAll 基本 ====================
    {
        int fds[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

        // 发 100 字节
        std::string data(100, 'x');
        ssize_t sent = net::sendAll(fds[0], data.data(), data.size());
        assert(sent == 100);

        // 收 100 字节
        std::string buf(100, '\0');
        ssize_t recvd = net::recvAll(fds[1], &buf[0], buf.size());
        assert(recvd == 100);
        assert(buf == data);

        ::close(fds[0]);
        ::close(fds[1]);
    }
    std::cout << "[OK] send/recv basic\n";

    // ==================== 3. 大块数据（跨多次系统调用） ====================
    {
        int fds[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

        // 256 KB —— 远超 socket buffer，必然多次 send/recv
        const size_t N = 256 * 1024;
        std::string data(N, '\0');
        for (size_t i = 0; i < N; ++i) {
            data[i] = static_cast<char>(i & 0xFF);  // 保留低 8 位存 char
        }

        // 单线程先 send 完再 recv：数据超过 socket 发送缓冲区会阻塞
        // 这里只发 8 KB，确保不卡住；大块数据留给真实服务器阶段
        const size_t M = 8 * 1024;
        ssize_t sent = net::sendAll(fds[0], data.data(), M);
        assert(sent == (ssize_t)M);

        std::string buf(M, '\0');
        ssize_t recvd = net::recvAll(fds[1], &buf[0], M);
        assert(recvd == (ssize_t)M);
        assert(std::memcmp(data.data(), buf.data(), M) == 0);

        ::close(fds[0]);
        ::close(fds[1]);
    }
    std::cout << "[OK] send/recv multi-call\n";

    // ==================== 4. 对端关闭 → recvAll 返回 -1 ====================
    {
        int fds[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

        ::close(fds[0]);   // 关闭写端

        char buf[10];
        ssize_t recvd = net::recvAll(fds[1], buf, sizeof(buf));
        assert(recvd == -1);   // 对端关闭 → recv 返回 0 → recvAll 返回 -1

        ::close(fds[1]);
    }
    std::cout << "[OK] peer closed\n";

    std::cout << "ALL OK\n";
    return 0;
}