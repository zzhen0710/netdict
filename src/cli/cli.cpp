/// @file cli/cli.cpp
/// @brief 客户端实现：连接、发送、接收、显示。

#include "cli/cli.hpp"
#include "common/logger.hpp"
#include "common/proto.hpp"

#include <arpa/inet.h>      // inet_addr
#include <netinet/in.h>     // sockaddr_in
#include <poll.h>           // poll, pollfd, POLLIN
#include <sys/socket.h>     // socket, connect, recv
#include <unistd.h>         // close, write

#include <cerrno>           // errno, EINTR
#include <iostream>
#include <stdexcept>        // std::runtime_error

// ------- 文件内常量 -------

namespace {

    /// 内存历史上限：超出的最旧记录被淘汰（环形）。
    /// cli 专用；ser 用别的（管理员命令历史可能更少）。
    constexpr int kCliHistoryMaxLen = 100;

    /// 历史文件名（拼到家目录）。
    /// cli 专用（~/.netdict_history）；ser 用别的。
    constexpr const char* kCliHistoryFile = "/.netdict_history";

}   // namespace

// ---------------- 构造 / 析构 ----------------

/// 构造：创建 socket 并连接服务器。
Cli::Cli(std::string_view ip, int port)
    : sock_fd_(-1),
      editor_history_(kCliHistoryMaxLen, kCliHistoryFile) {

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

    // 历史由 editor_history_ 成员在初始化列表里构造（Load）；
    // 析构时自动 Save（全量覆盖）。文件路径：$HOME + kCliHistoryFile。
}

/// 析构：关闭 socket（历史由 editor_history_ 自动保存）。
Cli::~Cli() {
    // editor_history_ 析构自动 Save，不用手动调

    if (sock_fd_ >= 0) {
        ::close(sock_fd_);
    }
}

// ---------------- 主循环 ----------------

/// 主循环：poll 等 stdin / socket，分发处理。
/// 每轮两个分支：socket 可读 → 探测断开；stdin 可读 → 喂编辑。
/// 直到 EOF / 断开 / .quit / stop()（信号）。
void Cli::run() {
    running_.store(true);   // 置运行标志

    // 欢迎信息
    printLine("welcome to netdict client");
    printLine("type .help for commands, Ctrl+D to quit");

    // poll 监听：stdin（用户输入）+ socket（服务器数据/断开）
    pollfd fds[2];
    fds[0].fd = STDIN_FILENO;  fds[0].events = POLLIN;
    fds[1].fd = sock_fd_;      fds[1].events = POLLIN;

    // 编辑会话：整个 run 期间一个，start/stop 手动切换。
    // 构造 = EditStart（进 raw mode），析构兜底 Stop。
    LineEditor editor(STDIN_FILENO, STDOUT_FILENO, "netdict> ");

    bool kicked = false;   // true = 被服务器终态通知踢下线（被动退出）

    // 主循环：等 stdin / socket 事件
    while (running_.load()) {
        int n = poll(fds, 2, -1);   // 阻塞等；-1 = 不限时
        if (n < 0) {
            if (errno == EINTR) continue;   // 信号打断，重试
            editor.stop();                  // 退编辑，回正常模式，才能打印
            LOG_ERR("poll failed");
            break;
        }

        // 分支 1：socket 可读（数据 / 挂断 / 错误）
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            char c;
            ssize_t r = recv(sock_fd_, &c, 1, MSG_PEEK);   // 偷看 1 字节，不消费

            // r == 0：对端关闭（FIN）。先退编辑（回正常模式），再打印。
            if (r == 0) {
                editor.stop();
                printLine("[server disconnected]");
                break;
            }
            // r > 0：有数据。此处是服务器主动推的通知（非"请求-响应"配对），
            //   但格式与响应一致，统一交给 handleResp 处理（它管所有回复）。
            //   handleResp 返回：
            //     true  = ok 状态词 → 非终态通知 → 继续等用户输入
            //     非 true = err / 其他 → 终态通知（如 idle timeout）→ 等键退出
            if (r > 0) {
                editor.stop();   // 退编辑，回正常模式，才能打印

                if (handleResp()) {
                    // 非终态：重新进编辑，继续等用户输入
                    editor.start();
                } else {
                    // 终态：等用户按键看清提示，再标记被动退出
                    kicked = true;
                    printLine("press Enter to exit...");
                    char ch;
                    ::read(STDIN_FILENO, &ch, 1);
                    break;
                }
            }
            // r < 0：错误，理论不会（poll 说可读），忽略
        }

        // 分支 2：stdin 可读（用户敲键）
        if (fds[0].revents & POLLIN) {
            auto r = editor.feed();   // 喂一个键；可能返回 More / Eof / Line

            if (r == LineEditor::FeedResult::More) {
                continue;   // 还在编辑（没回车）
            }
            if (r == LineEditor::FeedResult::Eof) {
                break;      // EOF（Ctrl+D）/ Ctrl+C
            }

            // Line：拿到整行。先退编辑（回正常模式），再处理、打印。
            std::string line = editor.line();
            editor.stop();

            // 非空行入历史
            if (!line.empty()) {
                editor_history_.add(line.c_str());
            }
            if (!handleCmd(line)) break;   // false = .quit，退出

            editor.start();   // 重新进编辑，准备下一行
        }
    }
    // editor 析构：兜底 Stop（幂等）
    
    // 被动退出（被服务器踢）：补 goodbye
    if (kicked) {
        printLine("goodbye");
    }

    LOG_DEBUG("client exit");   // 唯一收尾
}

/// 请求停止：置 running_ = false（信号处理调）。
void Cli::stop() {
    running_.store(false);
}

// ---------------- 命令处理 ----------------

/// 处理一行用户命令：空行跳过，发送，收响应，显示。
/// @return 是否继续主循环（false = .quit，应退出）
bool Cli::handleCmd(std::string_view line) {
    LOG_DEBUG("cmd: %.*s", static_cast<int>(line.size()), line.data());

    // 空行跳过
    if (line.empty()) return true;

    // 未发送（格式错），继续
    if (!sendReq(line)) return true;

    bool ok = handleResp();

    // .logout：ok 后打 goodbye；未登录服务器回 err，不打
    if (ok && line == ".logout") {
        printLine("goodbye");
    }

    // .quit / .exit：服务器必回 ok；显式判，避免服务器异常
    if (ok && (line == ".quit" || line == ".exit")) {
        printLine("goodbye");
        return false;   // 告诉 run 退出
    }

    return true;
}

/// 处理并发送用户输入：必须 "." 开头；否则打提示。
bool Cli::sendReq(std::string_view usr_req) {
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

    if (net::sendAll(sock_fd_, out.data(), out.size()) < 0) {
        LOG_ERR("send failed");
        return false;
    }
    
    LOG_DEBUG("sent: %.*s", static_cast<int>(usr_req.size()), usr_req.data());

    return true;
}

/// 处理响应：解析首行；err → 打错误信息；ok → 打数据。
/// 若 data 为数字 n，则再读 n 行并打印；否则 data 本身即结果。
/// （不打 "ok" 前缀，只打结果）
bool Cli::handleResp() {
    std::string line;

    // 收首行；对端关闭 / 出错 → 打提示 + 抛（让 run 退出）
    if (net::recvLine(sock_fd_, recv_buf_, line) != net::RecvLineResult::Ok) {
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
        if (net::recvLine(sock_fd_, recv_buf_, line) != net::RecvLineResult::Ok) {
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