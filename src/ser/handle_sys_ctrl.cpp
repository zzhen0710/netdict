/// @file ser/handle_sys_ctrl.cpp
/// @brief Server 管理终端：控制命令（stat / history / pad / log / shutdown / help）。
/// 输出到 stdout（utils::printLine），无 cfd。

#include "ser/ser.hpp"
#include "common/logger.hpp"
#include "common/proto.hpp"
#include "common/utils.hpp"

// ---- 分发 ----

/// 处理管理终端控制命令：按枚举分发。
void Server::handleSysCtrl(const proto::Msg& msg, proto::SysCmd::Ctrl c) {
    switch (c) {
        case proto::SysCmd::Ctrl::Stat:     return doStat    (msg);
        case proto::SysCmd::Ctrl::History:  return doSysHistory(msg);
        case proto::SysCmd::Ctrl::Pad:      return doSysPad  (msg);
        case proto::SysCmd::Ctrl::Log:      return doLog     (msg);
        case proto::SysCmd::Ctrl::Shutdown: return doShutdown(msg);
        case proto::SysCmd::Ctrl::Help:     return doSysHelp (msg);
    }
}

// ---- 各命令 ----

/// .stat <usrname> —— 用户信息（账户是否存在 / 在线状态 / 历史数 / 收藏数）。
void Server::doStat(const proto::Msg& msg) {
    if (msg.args.empty()) {
        utils::printLine("usage: .stat <usrname>");
        return;
    }
    const std::string& name = msg.args[0];

    // 账户是否存在：不存在直接返回，后面不再查
    if (!usr_.exists(name)) {
        utils::printLine("no such user: " + name);
        return;
    }

    // 在线状态：conns_ 里任一连接的 usr == name 即视为在线（用户名唯一）
    // 注意：需加锁，conns_ 会被 handleClient 的收尾临界区修改
    bool online = false;
    {
        std::lock_guard lk(conns_mtx_);
        for (const auto& pair : conns_) {
            if (pair.second.usr == name) { online = true; break; }
        }
    }

    // 历史 / 收藏条数
    // 注意：limit 固定 1000，超过 1000 的计数会被截断，只是粗看
    std::vector<HistoryEntry> hist;
    std::vector<StarEntry>    stars;
    usr_.getHistory(name, 1000, hist);
    usr_.getStars(name, 1000, stars);

    utils::printLine("user   : " + name);
    utils::printLine("status : " + std::string(online ? "online" : "offline"));
    utils::printLine("history: " + std::to_string(hist.size()));
    utils::printLine("star   : " + std::to_string(stars.size()));
}

/// .history <usrname> [num] —— 看指定用户历史。
void Server::doSysHistory(const proto::Msg& msg) {
    if (msg.args.empty()) {
        utils::printLine("usage: .history <usrname> [num]");
        return;
    }

    const std::string& name = msg.args[0];

    // num 可选，默认 10；非法输入回退 10，夹在 [1,100]
    int num = 10;
    if (msg.args.size() >= 2) {
        try { num = std::stoi(msg.args[1]); } catch (...) { num = 10; }
        if (num <= 0) num = 10;
        if (num > 100) num = 100;
    }

    std::vector<HistoryEntry> out;
    if (!usr_.getHistory(name, static_cast<size_t>(num), out)) {
        utils::printLine("query failed");
        return;
    }
    if (out.empty()) {
        utils::printLine("(empty)");
        return;
    }

    // 按 (word, batch) 换词头；同一批的多释义归在一个词头下
    std::string last_word;
    long long   last_batch = -1;
    for (const auto& e : out) {
        if (e.word != last_word || e.batch != last_batch) {
            utils::printLine(e.word + "  [" + e.time + "]");
            last_word  = e.word;
            last_batch = e.batch;
        }
        utils::printLine("  " + e.pos + " " + e.mean);
    }
}

/// .pad <usrname> [num] —— 看指定用户收藏。
void Server::doSysPad(const proto::Msg& msg) {
    if (msg.args.empty()) {
        utils::printLine("usage: .pad <usrname> [num]");
        return;
    }

    const std::string& name = msg.args[0];

    // num 可选，默认 10；非法输入回退 10，夹在 [1,100]
    int num = 10;
    if (msg.args.size() >= 2) {
        try { num = std::stoi(msg.args[1]); } catch (...) { num = 10; }
        if (num <= 0) num = 10;
        if (num > 100) num = 100;
    }

    std::vector<StarEntry> out;
    auto st = usr_.getStars(name, static_cast<size_t>(num), out);
    if (st != status::Query::Ok) {
        utils::printLine("query failed");
        return;
    }
    if (out.empty()) {
        utils::printLine("(empty)");
        return;
    }

    // 收藏按 word 分组，同词多义在一个词头下；换 word 才换词头
    std::string last_word;
    for (const auto& e : out) {
        if (e.word != last_word) {
            utils::printLine(e.word + "  [" + e.time + "]");
            last_word = e.word;
        }
        utils::printLine("  " + e.pos + " " + e.mean);
    }
}

/// .log <level> —— 切日志等级（debug / info / warn / err / off）。
void Server::doLog(const proto::Msg& msg) {
    if (msg.args.empty()) {
        utils::printLine("usage: .log <debug|info|warn|err|off>");
        return;
    }

    // 解析等级名；不合法就拒绝，不改动当前等级
    logger::Level lv;
    if (!logger::parseLevel(msg.args[0].c_str(), lv)) {
        utils::printLine("bad level");
        return;
    }
    logger::setLevel(lv);
    utils::printLine("log level = " + msg.args[0]);
}

/// .shutdown / .quit / .exit —— 关闭服务器。
void Server::doShutdown(const proto::Msg& msg) {
    (void)msg;
    utils::printLine("server shutting down...");
    // 只置停止标志；主循环下一轮退出 → doStop 做清理
    requestStop();
}

/// .help —— 指令集。
void Server::doSysHelp(const proto::Msg& msg) {
    (void)msg;

    // 静态文案：分节列出账号 / 查询 / 收藏 / 杂项 / 编辑键
    static const char* lines[] = {
        "netdict admin console",
        "",
        "[dict lookup]",
        "  .list [name*]            list words (wildcard *)",
        "  .view <name>             view a word",
        "  .num                     entry count",
        "",
        "[dict edit]",
        "  .add <name> <pos> <mean>     add a word",
        "  .del <name>                  delete a word",
        "  .update <name> <pos> <mean>  update a word",
        "  .reload                      reload data/dict.txt",
        "",
        "[user]",
        "  .stat <usrname>          user info (history/star count)",
        "  .history <usrname> [num] user history",
        "  .pad <usrname> [num]     user pad",
        "",
        "[server]",
        "  .log <level>             set log level",
        "  .shutdown                shutdown server",
        "",
        "[misc]",
        "  .help                    show this help",
        "",
        "[edit]",
        "  ← / →                    move cursor",
        "  Home / End (Ctrl+A/E)    line start / end",
        "  Backspace / Delete       delete char",
        "  Ctrl+W                   delete word",
        "  Ctrl+U                   clear line",
        "  ↑ / ↓                    history",
        "",
    };
    for (const char* l : lines) {
        utils::printLine(l);
    }
}