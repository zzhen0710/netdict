/// @file ser/handle_usr_dict.cpp
/// @brief Server 用户字典命令：handleUsrDict 分发 + doQuery / doHistory / doStar / doUnstar / doPad。
///
/// 响应（分组结构，供客户端渲染）：
///   ok <词数>
///   --- <word>                 ← 词头
///   <pos>\t<mean>              ← 释义行（query/list）
///   <pos>\t<mean>\t<time>      ← 释义行（pad/history，带时间）
///   --- <word>
///   ...
///
/// 客户端解析后渲染成：
///   word
///     1. pos mean
///     2. pos mean

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

/// .query <word> —— 查词；分组响应。
/// 先收集所有行（词头 + 每释义一行），再发 "ok <总行数>" + 逐行。
/// 需登录；查完记历史（一个词的所有释义，事务）。
void Server::doQuery(int cfd, const proto::Msg& msg) {
    // 会话检查（未登录不能查）
    auto name = usrGet(cfd);
    if (!name) {
        sendLine(cfd, proto::makeErr("err", "not logged in"));
        return;
    }

    // 参数检查：必须有 word
    if (msg.args.empty()) {
        sendLine(cfd, proto::makeErr("bad_args"));
        return;
    }

    // 查字典；非 Ok（NotFound/Err）直接回状态
    const std::string& word = msg.args[0];
    std::vector<Meaning> out;
    auto st = dict_.query(word, out);

    if (st != status::Query::Ok) {
        sendLine(cfd, proto::makeErr(proto::Stat2Str(st)));
        return;
    }

    // 收集所有行：词头 + 每释义一行
    std::vector<std::string> lines;
    lines.reserve(1 + out.size());      // 预留：词头 + 释义数
    lines.push_back("--- " + word);     // 拼词头，供客户端识别分组
    for (const auto& m : out) {
        lines.push_back(m.pos + "\t" + m.mean);   // 每条释义：词性 + 释义
    }

    // 发：ok <总行数> + 逐行
    sendLine(cfd, proto::makeOk(std::to_string(lines.size())));
    for (const auto& l : lines) {
        sendLine(cfd, l);
    }

    // 记历史（一个词的所有释义，事务）
    usr_.addHistory(*name, word, out, utils::now());
}

/// .history [num] —— 查自己的历史（默认 10 次查询）；分组响应。
/// 判 (word, batch) 归组：同一次查询的多释义一行词头；
/// 同一 word 的多次查询 = 多个词头。
void Server::doHistory(int cfd, const proto::Msg& msg) {
    // 会话检查（未登录不能用）
    auto name = usrGet(cfd);
    if (!name) {
        sendLine(cfd, proto::makeErr("err", "not logged in"));
        return;
    }

    // 参数 num（可选，默认 10）
    int num = 10;
    if (!msg.args.empty()) {
        try {
            num = std::stoi(msg.args[0]);
        } catch (...) {
            sendLine(cfd, proto::makeErr("bad_args"));   // 非数字
            return;
        }
        if (num <= 0) num = 10;      // 下限：<=0 当默认
        if (num > 100) num = 100;    // 上限：最多 100
    }

    // 查历史（按 rowid desc，最近的在前）
    std::vector<HistoryEntry> out;
    if (!usr_.getHistory(*name, static_cast<size_t>(num), out)) {
        sendLine(cfd, proto::makeErr("err", "query failed"));
        return;
    }

    // 空：发一行纯文本提示（客户端原样打）
    if (out.empty()) {
        sendLine(cfd, proto::makeOk("1"));  // ok 1 下还有 1 行数据
        sendLine(cfd, "(history is empty)");
        return;
    }

    // 收集所有行：判 (word, batch) 换词头
    std::vector<std::string> lines;          
    std::string last_word;                   // 上一个词头（用于分组）
    long long   last_batch = -1;             // 上一批号（同词多批要分开）
    // 遍历历史：新 (word, batch) 就起一个词头，后面跟释义行
    for (const auto& e : out) {
        // word 变了 或 batch 变了 → 新的一组，起词头（带 time）
        if (e.word != last_word || e.batch != last_batch) {
            lines.push_back("--- " + e.word + "\t" + e.time);   // 拼词头，供客户端识别分组
            last_word  = e.word;
            last_batch = e.batch;
        }
        lines.push_back(e.pos + "\t" + e.mean);   // 释义行
    }

    // 发：ok <总行数> + 逐行
    sendLine(cfd, proto::makeOk(std::to_string(lines.size())));
    for (const auto& l : lines) {
        sendLine(cfd, l);
    }
}

/// .star <word> —— 收藏单词（原子：全部释义）。
/// 回 "ok" / "starred"（已收藏）。
void Server::doStar(int cfd, const proto::Msg& msg) {
    // 会话检查
    auto name = usrGet(cfd);
    if (!name) {
        sendLine(cfd, proto::makeErr("err", "not logged in"));
        return;
    }

    // 参数检查
    if (msg.args.empty()) {
        sendLine(cfd, proto::makeErr("bad_args"));
        return;
    }

    // 先查 dict 拿全部释义
    std::vector<Meaning> ms;
    auto qs = dict_.query(msg.args[0], ms);
    if (qs != status::Query::Ok) {
        sendLine(cfd, proto::makeErr(proto::Stat2Str(qs)));
        return;
    }

    // 收藏（原子：插全部释义；time 由服务器取）
    auto st = usr_.star(*name, msg.args[0], ms, utils::now());
    if (st == status::Query::Ok) {
        LOG_INFO("star: usr = %s, word = %s", name->c_str(), msg.args[0].c_str());
        sendLine(cfd, proto::makeOk());
    } else {
        sendLine(cfd, proto::makeErr(proto::Stat2Str(st)));
    }
}

/// .unstar <word> —— 取消收藏（删全部释义）；回 "ok" / "unstarred"。
void Server::doUnstar(int cfd, const proto::Msg& msg) {
    // 会话检查
    auto name = usrGet(cfd);
    if (!name) {
        sendLine(cfd, proto::makeErr("err", "not logged in"));
        return;
    }

    // 参数检查
    if (msg.args.empty()) {
        sendLine(cfd, proto::makeErr("bad_args"));
        return;
    }

    // 取消收藏（删该 word 所有行）
    auto st = usr_.unstar(*name, msg.args[0]);
    if (st == status::Query::Ok) {
        LOG_INFO("star: usr = %s, word = %s", name->c_str(), msg.args[0].c_str());
        sendLine(cfd, proto::makeOk());
    } else {
        sendLine(cfd, proto::makeErr(proto::Stat2Str(st)));
    }
}

/// .pad [num] —— 显示收藏（字母序）；分组响应。
/// 词头带 time（同词多释义 time 相同，放词头一次，避免每行冗余）。
void Server::doPad(int cfd, const proto::Msg& msg) {
    // 会话检查（未登录不能用）
    auto name = usrGet(cfd);
    if (!name) {
        sendLine(cfd, proto::makeErr("err", "not logged in"));
        return;
    }

    // 参数 num（可选，默认 10）
    int num = 10;
    if (!msg.args.empty()) {
        try {
            num = std::stoi(msg.args[0]);
        } catch (...) {
            sendLine(cfd, proto::makeErr("bad_args"));   // 非数字
            return;
        }
        if (num <= 0) num = 10;      // 下限：<=0 当默认
        if (num > 100) num = 100;    // 上限：最多 100
    }

    // 取收藏（按 word 字母序）
    std::vector<StarEntry> out;
    auto st = usr_.getStars(*name, static_cast<size_t>(num), out);
    if (st != status::Query::Ok) {
        sendLine(cfd, proto::makeErr(proto::Stat2Str(st)));
        return;
    }

    // 空：发一行纯文本提示（客户端原样打）
    if (out.empty()) {
        sendLine(cfd, proto::makeOk("1"));  // ok 下还有 1 行数据
        sendLine(cfd, "(pad is empty)");
        return;
    }

    // 收集所有行：同 word 的连续记录归到同一词头下（getStars 已按 word 排序）。
    // 词头带 time（同词多释义 time 相同，取自第一条）；释义行只 pos\tmean。
    std::vector<std::string> lines;
    std::string last_word;
    for (const auto& e : out) {
        if (e.word != last_word) {                      // 新词：先起词头 + time
            lines.push_back("--- " + e.word + "\t" + e.time);   // 拼词头，供客户端识别分组
            last_word = e.word;
        }
        lines.push_back(e.pos + "\t" + e.mean);         // 释义行
    }

    // 发：ok <总行数> + 逐行
    sendLine(cfd, proto::makeOk(std::to_string(lines.size())));
    for (const auto& l : lines) {
        sendLine(cfd, l);
    }
}