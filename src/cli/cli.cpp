/// @file cli/cli.cpp
/// @brief 客户端实现：连接、发送、接收、显示。

#include "cli/cli.hpp"
#include "common/logger.hpp"
#include "common/proto.hpp"

/* linenoise.h — 轻量命令行编辑库
 *
 * 来源：https://github.com/antirez/linenoise
 * 许可：BSD-2-Clause（见源码头注释）
 * 用途：为本项目 cli 提供行编辑 + 历史（上下键翻）
 *
 * 本项目只用到的接口：
 *   linenoise(prompt)       读一行；返回 malloc 串（或 nullptr）
 *   linenoiseFree(p)        释放 linenoise 返回的串
 *   linenoiseHistoryAdd(s)  把一行加入内存历史（供 ↑/↓）
 *
 * 改动：无（原文件）
 */
#include "linenoise.h"

#include <arpa/inet.h>      // inet_addr
#include <netinet/in.h>     // sockaddr_in
#include <sys/socket.h>     // socket, connect, recv
#include <unistd.h>         // close, write

#include <cstring>          // memset
#include <iostream>
#include <stdexcept>        // std::runtime_error

// ------- 文件内常量 -------
namespace {

    /// linenoise 历史内存上限：超出的最旧记录被淘汰（环形）。
    /// 退出 Save 全量覆盖写文件，文件行数 ≤ 此值。
    constexpr int kHistoryMaxLen = 100;

    /// 历史文件：家目录下（用户级数据，不随 cwd / 项目走）。
    constexpr const char* kHistoryFile = "/.netdict_history";

    /// 返回 linenoise 历史文件路径：~/.netdict_history。
    /// 历史是用户级数据，放家目录（不随 cwd / 项目）；无 HOME 兜底当前目录。
    std::string historyPath() {
        const char* home = std::getenv("HOME");
        return home ? std::string(home) + kHistoryFile
                    : std::string(".") + kHistoryFile;
    }

}   // namespace

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

    LOG_DEBUG("connected to %.*s:%d",
         static_cast<int>(ip.size()), ip.data(), port);

    // 历史：内存最多 kHistoryMaxLen 条（超出的最旧淘汰）；
    // 退出时 Save 全量覆盖写文件（非追加），文件行数 ≤ kHistoryMaxLen。
    linenoiseHistorySetMaxLen(kHistoryMaxLen);
    linenoiseHistoryLoad(historyPath().c_str());
}

/// 析构：保存历史；关闭 socket。
Cli::~Cli() {
    // 退出前保存历史（全量覆盖）；下次启动 Load 可 ↑ 翻。
    linenoiseHistorySave(historyPath().c_str());
    
    if (sock_fd_ >= 0) {
        ::close(sock_fd_);
    }
}

/// 主循环：读 stdin → 发请求 → 收响应 → 显示。
void Cli::run() {
    running_ = true;

    printLine("welcome to netdict client");
    printLine("type .help for commands, Ctrl+D to quit");

    std::string line;
    // 读一行 → 空行跳过 → 发送 → 收响应；EOF 退出
    while (running_.load() && readUsrLine(line)) {
        LOG_DEBUG("cmd: %.*s", static_cast<int>(line.size()), line.data());

        if (line.empty()) continue;
        if (!sendRequest(line)) continue;

        bool ok = handleResp();

        // .logout：ok 后打 goodbye；未登录服务器回 err，不打
        if (ok && line == ".logout") {
            printLine("goodbye");
        }

        // .quit / .exit：服务器必回 ok；显式判，避免服务器异常
        if (ok && (line == ".quit" || line == ".exit")) {
            printLine("goodbye");
            break;
        }
    }

    LOG_DEBUG("client exit");
}

/// 请求停止：置 running_ = false（信号处理调）。
void Cli::stop() {
    running_.store(false);
}

/// 读一行用户输入；EOF / Ctrl+C 返回 false。
/// 用 linenoise：支持行编辑、上下键翻历史。
bool Cli::readUsrLine(std::string& line) {
    // linenoise(prompt)：打印提示符，读一行；内部处理行编辑 / 上下键。
    //   - 返回 malloc 出的 C 串，需 linenoiseFree 释放；
    //   - EOF（Ctrl+D）或 Ctrl+C 中止时返回 nullptr。
    char* raw = linenoise("netdict> ");
    if (!raw) return false;

    line = raw;                                 // C 串 → std::string（拷贝）

    // 加入内存历史，之后可用 ↑ / ↓ 翻。空行不入。
    if (!line.empty()) linenoiseHistoryAdd(raw);

    linenoiseFree(raw);                         // 释放 linenoise 内部 malloc

    return true;
}

/// 处理并发送用户输入：必须 "." 开头；否则打提示。
bool Cli::sendRequest(std::string_view usr_req) {
    // 必须 "." 开头
    if (usr_req.empty() || usr_req.front() != '.') {
        printLine("commands must start with '.' (type .help)");
        return false;
    }
    usr_req.remove_prefix(1);   // 去 "."

    // 只剩 "."：空命令
    if (usr_req.empty()) {
        printLine("empty command (type .help)");
        return false;
    }

    // 拼 "cmd args\n" 并发
    std::string out(usr_req);
    out += '\n';

    LOG_DEBUG("sent: %.*s", static_cast<int>(usr_req.size()), usr_req.data());

    if (net::sendAll(sock_fd_, out.data(), out.size()) < 0) {
        LOG_ERR("send failed"); 
        return false;
    }

    return true;
}

/// 处理响应：解析首行；err → 打错误信息；ok → 打数据。
/// 若 data 为数字 n，则再读 n 行并打印；否则 data 本身即结果。
/// （不打 "ok" 前缀，只打结果）
bool Cli::handleResp() {
    std::string line;

    // 收首行；对端关闭 / 出错 → 打提示 + 抛（让 run 退出）
    if (!net::recvLine(sock_fd_, recv_buf_, line)) {
        printLine("[server disconnected]");
        throw std::runtime_error("server disconnected");
    }

    LOG_DEBUG("recv: %.*s", static_cast<int>(line.size()), line.data());

    // 失败：err / xxx 前缀；打错因后返回
    if (!proto::isOk(line)) {
        std::string_view stat = proto::respStat(line);
        if (stat == "err") {
            // err 的 "reason" 在首词之后；用 respReason 取
            printLine(std::string("error: ") + std::string(proto::respReason(line)));
        } else {
            printLine(stat);
        }
        return false;
    }

    // 成功：从 "ok [data]" 里取出 data（不含 "ok"）
    std::string_view data = proto::respData(line);

    // data 全数字 → 多行响应（后续还有 n 行数据）
    bool is_count = !data.empty();
    for (char c : data) {
        if (c < '0' || c > '9') { is_count = false; break; }
    }

    // 单行：data 即结果本身；空表示 "ok" 无数据，直接返回
    if (!is_count) {
        if (!data.empty()) printLine(data);
        return true;
    }

    // 多行：读 n 行数据（每行打原样）
    int n = std::stoi(std::string(data));
    printLine(std::to_string(n) + " result(s):");
    for (int i = 0; i < n; ++i) {
        if (!net::recvLine(sock_fd_, recv_buf_, line)) {
            printLine("[server disconnected]");
            throw std::runtime_error("server disconnected");
        }
        LOG_DEBUG("recv: %.*s", static_cast<int>(line.size()), line.data());

        printLine(line);
    }

    return true;
}

/// 打印一行。
void Cli::printLine(std::string_view s) {
    // 服务器按长度发送（可能不含 '\0'），这里同样显式按长度写出，
    // 不依赖 '\0'，避免越界或截断。
    std::cout.write(s.data(), static_cast<std::streamsize>(s.size()));
    std::cout << '\n';
}