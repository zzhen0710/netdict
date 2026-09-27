/// @file ser/handle_usr_dict.cpp
/// @brief Server 用户字典命令：handleUsrDict 分发 + doQuery/doHistory/doStar/doPad。

#include "ser/ser.hpp"
#include "common/logger.hpp"
#include "common/proto.hpp"
#include "common/utils.hpp"

// ---- 分发 ----

/// 处理用户字典命令：按枚举分发到具体 doXxx。
void Server::handleUsrDict(int cfd, const proto::Msg& msg,
                           proto::UsrCmd::Dict c) {
    switch (c) {
        case proto::UsrCmd::Dict::Query:   return doQuery  (cfd, msg);
        case proto::UsrCmd::Dict::History: return doHistory(cfd, msg);
        case proto::UsrCmd::Dict::Star:    return doStar   (cfd, msg);
        case proto::UsrCmd::Dict::Unstar:  return doUnstar (cfd, msg);
        case proto::UsrCmd::Dict::Pad:     return doPad    (cfd, msg);
    }
}

// ---- 各命令 ----

/// .query <word> —— 查词；多行响应：ok <n> + n 行 "word\tmean"。
/// 需登录；查完记一条历史。
void Server::doQuery(int cfd, const proto::Msg& msg) {
    // 会话检查（未登录不能查）
    auto it = sessions_.find(cfd);
    if (it == sessions_.end()) {
        sendLine(cfd, proto::makeErr("err", "not logged in"));
        return;
    }
    const std::string& user = it->second;

    // 参数检查
    if (msg.args.empty()) {
        sendLine(cfd, proto::makeErr("bad_args"));
        return;
    }
    
    // 查字典
    const std::string& word = msg.args[0];
    std::vector<Meaning> out;
    auto st = dict_.query(word, out);

    if (st != stat::Query::Ok) {
        sendLine(cfd, proto::makeErr(proto::Stat2Str(st)));
        return;
    }

    // 成功：首行 "ok <n>"，后跟 n 行 "word\tmean"
    sendLine(cfd, proto::makeOk(std::to_string(out.size())));
    for (const auto& m : out) {
        sendLine(cfd, word + "\t" + m.text);
    }

    // 记历史（只记第一条，避免一次查询写多条）
    HistoryEntry entry{ word, out[0].text, utils::now() };
    usr_.addHistory(user, entry);
}

/// .history [num] —— 查自己的历史（默认 10 条）。
/// 多行响应：ok <n> + n 行 "word\tmean\ttime"。
void Server::doHistory(int cfd, const proto::Msg& msg) {
    // 会话检查
    auto it = sessions_.find(cfd);
    if (it == sessions_.end()) {
        sendLine(cfd, proto::makeErr("err", "not logged in"));
        return;
    }

    // 参数 num（可选，默认为 10）
    int num = 10;
    if (!msg.args.empty()) {
        try {
            num = std::stoi(msg.args[0]);
        } catch(...) {  // "..."捕获所有异常
            sendLine(cfd, proto::makeErr("bad_args"));
            return;
        }
        if (num <= 0) num = 10;
        if (num > 100) num = 100;
    }

    // 查历史
    std::vector<HistoryEntry> out;
    if (!usr_.getHistory(it->second, static_cast<int>(num), out)) {
        sendLine(cfd, proto::makeErr("err", "query failed"));
        return;
    }

    // 多行：ok <n> + n 行 "word\tmean\ttime"
    sendLine(cfd, proto::makeOk(std::to_string(out.size())));
    for (const auto& e : out) {
        sendLine(cfd, e.word + "\t" + e.mean + "\t" + e.time);
    }
}

/// .star <word> —— 收藏单词；回 "ok" / "starred"（已收藏）。
void Server::doStar(int cfd, const proto::Msg& msg) {
    // 会话检查
    auto it = sessions_.find(cfd);
    if (it == sessions_.end()) {
        sendLine(cfd, proto::makeErr("err", "not logged in"));
        return;
    }

    // 参数检查
    if (msg.args.empty()) {
        sendLine(cfd, proto::makeErr("bad_args"));
        return;
    }

    // 先查 dict 拿 mean
    std::vector<Meaning> ms;
    auto qs = dict_.query(msg.args[0], ms);
    if (qs != stat::Query::Ok) {
        sendLine(cfd, proto::makeErr(proto::Stat2Str(qs)));
        return;
    }
    std::string mean = ms.empty() ? "" : ms[0].text;

    // 造 StarEntry（Server 给 time）
    StarEntry e{ msg.args[0], mean, utils::now() };

    // 收藏
    auto st = usr_.star(it->second, e);
    if (st == stat::Query::Ok) {
        sendLine(cfd, proto::makeOk());
    } else {
        sendLine(cfd, proto::makeErr(proto::Stat2Str(st)));
    }
}

/// .unstar <word> —— 取消收藏；回 "ok" / "unstarred"（本来就没收藏）。
void Server::doUnstar(int cfd, const proto::Msg& msg) {
    // 会话检查
    auto it = sessions_.find(cfd);
    if (it == sessions_.end()) {
        sendLine(cfd, proto::makeErr("err", "not logged in"));
        return;
    }

    // 参数检查
    if (msg.args.empty()) {
        sendLine(cfd, proto::makeErr("bad_args"));
        return;
    }

    // 取消收藏
    auto st = usr_.unstar(it->second, msg.args[0]);
    if (st == stat::Query::Ok) {
        sendLine(cfd, proto::makeOk());
    } else {
        sendLine(cfd, proto::makeErr(proto::Stat2Str(st)));
    }
}

/// .pad —— 显示收藏（字母序）；多行：ok <n> + n 行 "word"。
void Server::doPad(int cfd, const proto::Msg& msg) {
    // 会话检查
    auto it = sessions_.find(cfd);
    if (it == sessions_.end()) {
        sendLine(cfd, proto::makeErr("err", "not logged in"));
        return;
    }

    // 参数 num（可选，默认 10）
    int num = 10;
    if (!msg.args.empty()) {
        try {
            num = std::stoi(msg.args[0]);
        } catch (...) {
            sendLine(cfd, proto::makeErr("bad_args"));
            return;
        }
        if (num <= 0) num = 10;
        if (num > 100) num = 100;
    }

    // 取收藏
    std::vector<StarEntry> out;
    auto st = usr_.getStars(it->second, static_cast<size_t>(num), out);
    if (st != stat::Query::Ok) {
        sendLine(cfd, proto::makeErr(proto::Stat2Str(st)));
        return;
    }

    // 多行: ok <n> + n 行 "word"
    sendLine(cfd, proto::makeOk(std::to_string(out.size())));
    for (const auto& e : out) {
        sendLine(cfd, e.word + "\t" + e.mean + "\t" + e.time);
    }
}