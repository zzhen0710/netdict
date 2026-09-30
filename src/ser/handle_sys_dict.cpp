/// @file ser/handle_sys_dict.cpp
/// @brief Server 管理终端：字典命令（list/view/add/del/update/reload/num）。

#include "ser/ser.hpp"
#include "common/logger.hpp"
#include "common/proto.hpp"
#include "common/utils.hpp"

void Server::handleSysDict(const proto::Msg& msg, proto::SysCmd::Dict c) {
    (void)msg; (void)c;   // 待实现
}