/// @file ser/handle_usr_ctrl.cpp
/// @brief Server 用户控制命令：handleUsrCtrl 分发 + doReg/doLogin/doLogout/doHelp。

#include "ser/ser.hpp"
#include "common/logger.hpp"
#include "common/proto.hpp"

// ---- 分发 ----

/// 处理用户控制命令：按枚举分发到具体 doXxx。
void Server::handleUsrCtrl(int cfd, const proto::Msg& msg,
                           proto::UsrCmd::Ctrl c) {
    switch (c) {
        case proto::UsrCmd::Ctrl::Reg:    return doReg   (cfd, msg);
        case proto::UsrCmd::Ctrl::Login:  return doLogin (cfd, msg);
        case proto::UsrCmd::Ctrl::Logout: return doLogout(cfd, msg);
        case proto::UsrCmd::Ctrl::Help:   return doHelp  (cfd, msg);
    }
}

// ---- 各命令 ----

/// .reg <name> <pwd> —— 注册（占位）。
void Server::doReg(int cfd, const proto::Msg& msg) {
    (void)msg;
    sendLine(cfd, proto::makeErr("err", "not implemented"));
}

/// .login <name> <pwd> —— 登录；成功后记会话。
void Server::doLogin(int cfd, const proto::Msg& msg) {
    if (msg.args.size() < 2) {
        sendLine(cfd, proto::makeErr("bad_args"));
        return;
    }
    auto st = usr_.login(msg.args[0], msg.args[1]);
    if (st == stat::UsrOp::Ok) {
        sessions_[cfd] = msg.args[0];   // 记会话
        sendLine(cfd, proto::makeOk());
    } else {
        sendLine(cfd, proto::makeErr(proto::Stat2Str(st)));
    }
}

/// .logout —— 登出；清会话。
void Server::doLogout(int cfd, const proto::Msg& msg) {
    (void)msg;
    auto it = sessions_.find(cfd);
    if (it == sessions_.end()) {
        sendLine(cfd, proto::makeErr("err", "not logged in"));
        return;
    }
    usr_.logout(it->second);
    sessions_.erase(it);
    sendLine(cfd, proto::makeOk());
}

/// .help —— 欢迎与指令集（占位）。
void Server::doHelp(int cfd, const proto::Msg& msg) {
    (void)msg;
    sendLine(cfd, proto::makeErr("err", "not implemented"));
}