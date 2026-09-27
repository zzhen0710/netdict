/// @file ser/handle_usr_ctrl.cpp
/// @brief Server 用户控制命令：handleUsrCtrl 分发 + doReg/doLogin/doLogout/doHelp。

#include "ser/ser.hpp"
#include "common/logger.hpp"
#include "common/proto.hpp"

#include <string>

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

// ---- 各命令实现（占位） ----

void Server::doReg(int cfd, const proto::Msg& msg) {
    (void)msg;
    sendLine(cfd, proto::makeErr("err", "not implemented"));
}

void Server::doLogin(int cfd, const proto::Msg& msg) {
    (void)msg;
    sendLine(cfd, proto::makeErr("err", "not implemented"));
}

void Server::doLogout(int cfd, const proto::Msg& msg) {
    (void)msg;
    sendLine(cfd, proto::makeErr("err", "not implemented"));
}

void Server::doHelp(int cfd, const proto::Msg& msg) {
    (void)msg;
    sendLine(cfd, proto::makeOk("commands: query history star pad reg login logout help"));
}