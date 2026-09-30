/// @file ser/handle_sys_ctrl.cpp
/// @brief Server 管理终端：控制命令（stat/history/pad/log/shutdown/help）。

#include "ser/ser.hpp"
#include "common/logger.hpp"
#include "common/proto.hpp"
#include "common/utils.hpp"

void Server::handleSysCtrl(const proto::Msg& msg, proto::SysCmd::Ctrl c) {
    (void)msg; (void)c;   // 待实现
}