/// @file common/net.hpp
/// @brief 网络常量与 IO 工具。

#pragma once

#include <cstddef>      // std::size_t
#include <string>       // std::string

namespace net {

    /// 默认监听 IP（0.0.0.0 = 监听所有网卡）
    constexpr const char* DEFAULT_IP = "0.0.0.0";

    /// 默认监听端口
    constexpr int DEFAULT_PORT = 13140;

    /// 单条消息缓冲大小（读写上限）
    constexpr std::size_t BUF_SIZE = 4096;


    /// 循环 send，直到发完 len 字节；成功返回 len，失败 -1。
    // TCP 不保证一次 send 发完（内核缓冲满 / 信号中断）；
    // 协议要求"一条消息完整送达"，故循环补发剩余部分。
    ssize_t sendAll(int fd, const char* buf, std::size_t len);


    /// 循环 recv，直到收满 len 字节；成功返回 len，失败 -1。
    // 适用"已知总长"的协议（定长 / 长度前缀 struct）；行协议用 recvLine。
    // （注：本项目未使用该函数，保留作通用 IO 工具）
    ssize_t recvAll(int fd, char* buf, std::size_t len);


    /// 收一行：从 recv_buf 里切出到 '\n' 为止的内容；对端关 / 出错返回 false。
    // recv_buf 由调用方持有，跨调用累积，不丢"下一行"的字节；
    // 缓冲无 '\n' 时再 recv（一次多字节），找到才切出一行。
    // 行协议不知行长，无法给 recvAll 传 len，故需要本函数。
    bool recvLine(int fd, std::string& recv_buf, std::string& line);

}   // namespace net