/// @file common/net.hpp
/// @brief 网络常量与 IO 工具。

#pragma once

#include <cstddef>   // std::size_t
#include <sys/types.h>   // ssize_t

namespace net {

/// 默认监听 IP（0.0.0.0 = 监听所有网卡）
constexpr const char* DEFAULT_IP = "0.0.0.0";

/// 默认监听端口
constexpr int DEFAULT_PORT = 13140;

/// 单条消息缓冲大小（读写上限）
constexpr std::size_t BUF_SIZE = 4096;

/// 循环 send，直到发完 len 字节；成功返回 len，失败 -1。
ssize_t sendAll(int fd, const char* buf, std::size_t len);

/// 循环 recv，直到收满 len 字节；成功返回 len，失败 -1。
ssize_t recvAll(int fd, char* buf, std::size_t len);

}   // namespace net