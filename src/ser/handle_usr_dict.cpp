/// @file ser/handle_usr_dict.cpp
/// @brief Server 用户字典命令：handleUsrDict 分发 + doQuery/doHistory/doStar/doPad。

#include "ser/ser.hpp"
#include "common/logger.hpp"
#include "common/proto.hpp"
#include "common/utils.hpp"

#include <string>
#include <vector>

// ---- 分发 ----

/// 处理用户字典命令：按枚举分发到具体 doXxx。
void Server::handleUsrDict(int cfd, const proto::Msg& msg,
                           proto::UsrCmd::Dict c) {
    switch (c) {
        case proto::UsrCmd::Dict::Query:   return doQuery  (cfd, msg);
        case proto::UsrCmd::Dict::History: return doHistory(cfd, msg);
        case proto::UsrCmd::Dict::Star:    return doStar   (cfd, msg);
        case proto::UsrCmd::Dict::Pad:     return doPad    (cfd, msg);
    }
}

// ---- 各命令 ----

/// .query <word> —— 查词；多行响应：ok <n> + n 行 "word | mean"。
void Server::doQuery(int cfd, const proto::Msg& msg) {
    // 参数检查
    if (msg.args.empty()) {
        sendLine(cfd, proto::makeErr("bad_args"));
        return;
    }

    // 查字典
    std::vector<Meaning> out;
    auto st = dict_.query(msg.args[0], out);

    // 失败：单行错误响应
    if (st != stat::Query::Ok) {
        sendLine(cfd, proto::makeErr(proto::Stat2Str(st)));
        return;
    }

    // 成功：首行 "ok <n>"，后跟 n 行 "word | mean"
    sendLine(cfd, proto::makeOk(std::to_string(out.size())));
    for (const auto& m : out) {
        sendLine(cfd, msg.args[0] + " | " + m.text);
    }
}

/// .history [num] —— 查自己的历史（默认 10 条）。
void Server::doHistory(int cfd, const proto::Msg& msg) {
    // TODO: 需要"当前登录用户"
    (void)msg;
    sendLine(cfd, proto::makeErr("err", "not implemented"));
}

/// .star <word> —— 收藏单词。
void Server::doStar(int cfd, const proto::Msg& msg) {
    // TODO: 需要"当前登录用户"
    (void)msg;
    sendLine(cfd, proto::makeErr("err", "not implemented"));
}

/// .pad —— 显示收藏（按字母序）。
void Server::doPad(int cfd, const proto::Msg& msg) {
    // TODO: 需要"当前登录用户"
    (void)msg;
    sendLine(cfd, proto::makeErr("err", "not implemented"));
}