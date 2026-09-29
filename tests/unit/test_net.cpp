/// test_net.cpp — net 常量与 IO 工具冒烟测试

#include "common/net.hpp"

#include <cassert>
#include <cerrno>        // errno
#include <cstring>       // std::memcmp, std::strcmp
#include <iostream>
#include <string>
#include <sys/socket.h>  // socketpair
#include <sys/time.h>    // struct timeval（SO_RCVTIMEO 用）
#include <unistd.h>      // close, write

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
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);   // 一对连通 fd

        // 发 100 字节，收回，比对
        std::string data(100, 'x');
        ssize_t sent = net::sendAll(fds[0], data.data(), data.size());
        assert(sent == 100);

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

        // 256KB 循环字节序列（每字节 i & 0xFF），便于校验
        const size_t N = 256 * 1024;
        std::string data(N, '\0');
        for (size_t i = 0; i < N; ++i) {
            data[i] = static_cast<char>(i & 0xFF);
        }

        // 只发 8 KB，确保不卡住；大块数据留给真实服务器阶段
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

        ::close(fds[0]);   // 先关对端

        char buf[10];
        ssize_t recvd = net::recvAll(fds[1], buf, sizeof(buf));
        assert(recvd == -1);   // 对端关闭，recvAll 失败

        ::close(fds[1]);
    }
    std::cout << "[OK] peer closed\n";

    // ==================== 5. recvLine：Ok（收到一行） ====================
    {
        int fds[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

        const char* msg = "hello\n";
        assert(net::sendAll(fds[0], msg, std::strlen(msg)) > 0);

        std::string recv_buf, line;
        auto r = net::recvLine(fds[1], recv_buf, line);

        assert(r == net::RecvLineResult::Ok);
        assert(line == "hello");   // '\n' 已剥离

        ::close(fds[0]);
        ::close(fds[1]);
    }
    std::cout << "[OK] recvLine Ok\n";

    // ==================== 6. recvLine：跨 recv 累积到 '\n' ====================
    // 数据分两次发；recvLine 内部第二次 recv 才凑齐一行
    {
        int fds[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

        // 先发半行，再发另一半
        assert(net::sendAll(fds[0], "wor", 3) > 0);
        assert(net::sendAll(fds[0], "ld\n", 3) > 0);

        std::string recv_buf, line;
        auto r = net::recvLine(fds[1], recv_buf, line);

        assert(r == net::RecvLineResult::Ok);
        assert(line == "world");   // 跨两次 recv 凑成一行

        ::close(fds[0]);
        ::close(fds[1]);
    }
    std::cout << "[OK] recvLine 跨 recv 累积\n";

    // ==================== 7. recvLine：多行粘包，剩余留在 recv_buf ====================
    {
        int fds[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

        // 一次发两行（粘包）：recvLine 应只切出第一行，第二行留缓冲
        const char* msg = "line1\nline2\n";
        assert(net::sendAll(fds[0], msg, std::strlen(msg)) > 0);

        std::string recv_buf, line;
        assert(net::recvLine(fds[1], recv_buf, line) == net::RecvLineResult::Ok);
        assert(line == "line1");

        // 缓冲里应还剩 "line2\n"；再 recvLine 不再 recv 就能拿到第二行
        assert(net::recvLine(fds[1], recv_buf, line) == net::RecvLineResult::Ok);
        assert(line == "line2");

        ::close(fds[0]);
        ::close(fds[1]);
    }
    std::cout << "[OK] recvLine 多行粘包\n";

    // ==================== 8. recvLine：Closed（对端关闭） ====================
    {
        int fds[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

        ::close(fds[0]);   // 对端关闭

        std::string recv_buf, line;
        auto r = net::recvLine(fds[1], recv_buf, line);
        assert(r == net::RecvLineResult::Closed);

        ::close(fds[1]);
    }
    std::cout << "[OK] recvLine Closed\n";

    // ==================== 9. recvLine：Timeout（SO_RCVTIMEO 超时） ====================
    // 给接收端设 100ms 超时；对端不发数据，recvLine 应返回 Timeout
    {
        int fds[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

        // 接收端设 SO_RCVTIMEO = 100ms
        struct timeval tv{};
        tv.tv_sec  = 0;
        tv.tv_usec = 100 * 1000;   // 100ms
        assert(setsockopt(fds[1], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0);

        // 对端不发数据；recvLine 会 recv 阻塞 100ms 后超时
        std::string recv_buf, line;
        auto r = net::recvLine(fds[1], recv_buf, line);
        assert(r == net::RecvLineResult::Timeout);
        assert(line.empty());

        ::close(fds[0]);
        ::close(fds[1]);
    }
    std::cout << "[OK] recvLine Timeout\n";

    std::cout << "ALL OK\n";
    return 0;
}