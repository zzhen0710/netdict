/// @file cli/cli.hpp
/// @brief 客户端：连服务器、读用户输入、发送请求、显示响应。

#pragma once

#include "common/line_editor.hpp"   // LineEditor + EditorHistory
#include "common/net.hpp"           // net::DEFAULT_IP / DEFAULT_PORT

#include <atomic>
#include <optional>
#include <string>
#include <string_view>

/// 命令行客户端：poll 同时等 stdin 和 socket。
/// stdin 可读 → linenoise 编辑；socket 可读 → 探测断开；
/// 拿到整行 → handleCommand（发送、收响应、显示）。
class Cli {
public:
    /// 连接服务器。
    /// @param ip    服务器 IP
    /// @param port  服务器端口
    /// @throws std::runtime_error 连接失败
    Cli(std::string_view ip = net::DEFAULT_IP,
        int port = net::DEFAULT_PORT);

    /// 析构：关闭 socket（历史由 editor_history_ 自动保存）。
    ~Cli();

    Cli(const Cli&) = delete;
    Cli& operator=(const Cli&) = delete;

    /// 主循环：poll 等 stdin / socket，分发处理。
    /// 直到 EOF（Ctrl+D）/ 断开 / .quit / stop()（信号）。
    void run();

    /// 请求停止：置 running_ = false（信号处理调）。
    void stop();

private:
    /// 处理一行用户命令：空行跳过，发送，收响应，显示。
    /// @return 是否继续主循环（false = .quit，应退出）
    bool handleCmd(std::string_view line);

    /// 处理并发送用户输入：要求前导 "." 并去前导 "."；空行返回 false。
    /// @return 是否已发送
    bool sendReq(std::string_view usr_req);

    /// 处理响应：解析 ok / err；ok <n> 时再读 n 行并显示。返回是否成功（ok）。
    bool handleResp();

    std::string       recv_buf_;        ///< 接收缓冲（攒到 \n）
    int               sock_fd_;         ///< 连接套接字
    std::atomic<bool> running_{false};  ///< 运行标志（信号 handler 与 run 竞争置 false）

    EditorHistory     editor_history_;              ///< 历史（进程级，构造 Load / 析构 Save）
    std::optional<LineEditor> editor_;              ///< 编辑会话（run 里 emplace，延迟构造）
};