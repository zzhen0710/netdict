/// @file cli/cli.hpp
/// @brief 客户端：连服务器、读用户输入、发送请求、显示响应。

#pragma once

#include "common/net.hpp"   // net::DEFAULT_IP / DEFAULT_PORT
#include <string>
#include <string_view>

/// 命令行客户端：阻塞"一问一答"。
/// 用户输入 ".query apple" → 去 "." → 发 "query apple\n" → 收响应 → 显示。
class Cli {
public:
    /// 连接服务器。
    /// @param ip    服务器 IP
    /// @param port  服务器端口
    /// @throws std::runtime_error 连接失败
    Cli(std::string_view ip = net::DEFAULT_IP,
        int port = net::DEFAULT_PORT);

    /// 析构：关闭 socket。
    ~Cli();

    Cli(const Cli&) = delete;
    Cli& operator=(const Cli&) = delete;

    /// 主循环：读 stdin → 发请求 → 收响应 → 显示；直到 EOF 或服务器断开。
    void run();

private:
    /// 读一行用户输入（含空格）；EOF 返回 false。
    bool readUsrLine(std::string& line);

    /// 处理并发送用户输入：要求前导 "." 并去前导 "."；空行返回 false。
    /// @return 是否已发送
    bool sendRequest(std::string_view usr_req);

    /// 处理响应：解析 ok / err；ok <n> 时再读 n 行并显示。返回是否成功（ok）。
    bool handleResp();

    /// 打印一行（stdout）。
    void printLine(std::string_view s);

    std::string recv_buf_;     ///< 接收缓冲（攒到 \n）
    int sock_fd_;              ///< 连接套接字
};