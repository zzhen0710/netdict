/// @file ser/handle_usr_ctrl.cpp
/// @brief Server 用户控制命令：handleUsrCtrl 分发 + doReg / doLogin / doLogout / doHelp。

#include "ser/ser.hpp"
#include "common/logger.hpp"
#include "common/proto.hpp"

#include <sys/socket.h>             // std::shutdown

// ---- 分发 ----

/// 处理用户控制命令：按枚举分发到具体 doXxx。
void Server::handleUsrCtrl(int cfd, const proto::Msg& msg,
                           proto::UsrCmd::Ctrl c) {
    switch (c) {
        case proto::UsrCmd::Ctrl::Reg:    return doReg   (cfd, msg);
        case proto::UsrCmd::Ctrl::Login:  return doLogin (cfd, msg);
        case proto::UsrCmd::Ctrl::Logout: return doLogout(cfd, msg);
        case proto::UsrCmd::Ctrl::Help:   return doHelp  (cfd, msg);
        case proto::UsrCmd::Ctrl::Quit:   return doQuit  (cfd, msg);
    }
}

// ---- 各命令 ----

/// .reg <name> <pwd> —— 注册；成功后自动登录。
/// 已登录用户不允许再注册（需先 .logout）。
void Server::doReg(int cfd, const proto::Msg& msg) {
    // 参数：<name> <pwd>（先查数量，避免 args[1] 越界 → UB）
    if (msg.args.size() < 2) {
        sendLine(cfd, proto::makeErr("bad_args"));
        return;
    }

    // 已登录：拦
    if (usrGet(cfd).has_value()) {      
        sendLine(cfd, proto::makeErr("err", "already logged in"));
        return;
    }

    auto st = usr_.reg(msg.args[0], msg.args[1]);
    if (st == status::UsrOp::Ok) {
        usrSet(cfd, msg.args[0]);     // 注册即登录 → 接入会话
        sendLine(cfd, proto::makeOk("welcome, " + msg.args[0]));
    } else {
        sendLine(cfd, proto::makeErr(proto::Stat2Str(st)));
    }
}

/// .login <name> <pwd> —— 登录；成功后记会话。
/// 已登录用户不允许再登录（需先 .logout）。
void Server::doLogin(int cfd, const proto::Msg& msg) {
    // 参数：<name> <pwd>（先查数量，避免 args[1] 越界 → UB）
    if (msg.args.size() < 2) {
        sendLine(cfd, proto::makeErr("bad_args"));
        return;
    }

    // 已登录：拦
    if (usrGet(cfd).has_value()) {
        sendLine(cfd, proto::makeErr("err", "already logged in"));
        return;
    }

    auto st = usr_.login(msg.args[0], msg.args[1]);
    if (st == status::UsrOp::Ok) {
        usrSet(cfd, msg.args[0]);     // 登录 → 接入会话
        LOG_INFO("login: usr = %s, fd = %d", msg.args[0].c_str(), cfd);
        sendLine(cfd, proto::makeOk("welcome, " + msg.args[0]));
    } else {
        sendLine(cfd, proto::makeErr(proto::Stat2Str(st)));
    }
}

/// .logout —— 登出；清会话。
void Server::doLogout(int cfd, const proto::Msg& msg) {
    (void)msg;

    if (!usrGet(cfd).has_value()) {
        sendLine(cfd, proto::makeErr("err", "not logged in"));
        return;
    }
    usrClear(cfd);

    sendLine(cfd, proto::makeOk());
}

/// .help —— 欢迎与指令集。
void Server::doHelp(int cfd, const proto::Msg& msg) {
    (void)msg;      // 不使用，防止 Warning

    // 多行响应：ok <n> + n 行文本
    static const char* lines[] = {
        "netdict - a tiny network english dictionary",
        "",
        "[account]",
        "  .reg   <name> <pwd>     register",
        "  .login <name> <pwd>     login",
        "  .logout                 logout",
        "",
        "[lookup]",
        "  .query   <word>         query a word",
        "  .history [num]          query history (default 10)",
        "",
        "[pad]",
        "  .star   <word>          add to pad",
        "  .unstar <word>          remove from pad",
        "  .pad    [num]           list pad (default 10)",
        "",
        "[misc]",
        "  .help                   show this help",
        "  .quit / .exit           quit",
        "",
        "[edit]",
        "  ← / →                   move cursor",
        "  Home / End (Ctrl+A/E)   line start / end",
        "  Backspace / Delete      delete char",
        "  Ctrl+W                  delete word",
        "  Ctrl+U                  clear line",
        "  ↑ / ↓                   history",
        "",
    };

    sendLine(cfd, proto::makeOk(std::to_string(sizeof(lines) / sizeof(lines[0]))));
    for (const char* l : lines) {
        sendLine(cfd, l);
    }
}

/// .quit / .exit —— 断开连接（客户端主动退出）。
/// 回 ok 后由 handleClient 退出循环。
void Server::doQuit(int cfd, const proto::Msg& msg) {
    (void)msg;
    sendLine(cfd, proto::makeOk());
    // 无需 shutdown：客户端收到 ok 后自己 close；handleClient 若继续 recv 会得 0 退出
    shutdown(cfd, SHUT_RDWR);   // 主动断读，促使 recv 返回 0
}