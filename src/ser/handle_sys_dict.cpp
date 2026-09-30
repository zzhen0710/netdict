/// @file ser/handle_sys_dict.cpp
/// @brief Server 管理终端：字典命令（list / view / add / del / update / reload / num）。
/// 输出到 stdout（utils::printLine），无 cfd。

#include "ser/ser.hpp"
#include "common/logger.hpp"
#include "common/proto.hpp"
#include "common/utils.hpp"

// ---- 分发 ----

/// 处理管理终端字典命令：按枚举分发。
void Server::handleSysDict(const proto::Msg& msg, proto::SysCmd::Dict c) {
    switch (c) {
        case proto::SysCmd::Dict::List:    return doList  (msg);
        case proto::SysCmd::Dict::View:    return doView  (msg);
        case proto::SysCmd::Dict::Add:     return doAdd   (msg);
        case proto::SysCmd::Dict::Del:     return doDel   (msg);
        case proto::SysCmd::Dict::Update:  return doUpdate(msg);
        case proto::SysCmd::Dict::Reload:  return doReload(msg);
        case proto::SysCmd::Dict::Num:     return doNum   (msg);
    }
}

// ---- 各命令 ----

/// .list [name*] —— 列词；参数是 SQL LIKE 模式（'*' 转 '%'；空 = "%"）。
void Server::doList(const proto::Msg& msg) {
    // 把用户打的 '*' 转成 SQL 的 '%'，空参数留给 dict_.list 当全匹配
    std::string pattern;
    if (!msg.args.empty()) {
        pattern = msg.args[0];
        for (char& ch : pattern) {
            if (ch == '*') ch = '%';
        }
    }

    std::vector<DictEntry> out;
    auto st = dict_.list(pattern, out);
    if (st != status::Admin::Ok) {
        utils::printLine("list failed");
        return;
    }

    // 先逐条：word \t pos \t mean，再条数
    for (const auto& e : out) {
        utils::printLine(e.word + "\t" + e.pos + "\t" + e.mean);
    }
    utils::printLine(std::to_string(out.size()) + " entr(ies):");
}

/// .view <word> —— 查看一个词（复用 dict_.query）。
void Server::doView(const proto::Msg& msg) {
    if (msg.args.empty()) {
        utils::printLine("usage: .view <word>");
        return;
    }

    std::vector<Meaning> out;
    auto st = dict_.query(msg.args[0], out);
    // query 与 Admin 状态不同：这里用 Query::NotFound / Ok
    if (st == status::Query::NotFound) {
        utils::printLine("not_found");
        return;
    }
    if (st != status::Query::Ok) {
        utils::printLine("query failed");
        return;
    }

    // 词头 + 编号释义
    utils::printLine(msg.args[0]);
    int idx = 0;
    for (const auto& m : out) {
        utils::printLine("  " + std::to_string(++idx) + ". "
                         + m.pos + " " + m.mean);
    }
}

/// .add <word> <pos> <mean> —— 加词（3 参数；pos 可空用 ""）。
void Server::doAdd(const proto::Msg& msg) {
    if (msg.args.size() < 3) {
        utils::printLine("usage: .add <word> <pos> <mean>");
        return;
    }

    Meaning m;
    m.pos  = msg.args[1];
    m.mean = msg.args[2];

    auto st = dict_.add(msg.args[0], m);
    utils::printLine(st == status::Admin::Ok ? "ok" : "add failed");
}

/// .del <word> —— 删词（该 word 所有释义）。
void Server::doDel(const proto::Msg& msg) {
    if (msg.args.empty()) {
        utils::printLine("usage: .del <word>");
        return;
    }

    auto st = dict_.del(msg.args[0]);
    // 三态：成功 / 本来就不存在 / 其他错误
    if (st == status::Admin::Ok)            utils::printLine("ok");
    else if (st == status::Admin::NotFound) utils::printLine("not_found");
    else                                    utils::printLine("del failed");
}

/// .update <word> <pos> <mean> —— 改词（该 word 释义全替换）。
void Server::doUpdate(const proto::Msg& msg) {
    if (msg.args.size() < 3) {
        utils::printLine("usage: .update <word> <pos> <mean>");
        return;
    }

    Meaning m;
    m.pos  = msg.args[1];
    m.mean = msg.args[2];

    auto st = dict_.update(msg.args[0], m);
    // 与 del 同构：Ok / NotFound / 其他
    if (st == status::Admin::Ok)            utils::printLine("ok");
    else if (st == status::Admin::NotFound) utils::printLine("not_found");
    else                                    utils::printLine("update failed");
}

/// .reload —— 清空 dict 表，从 data/dict.txt 重新导入。
void Server::doReload(const proto::Msg& msg) {
    (void)msg;   // 不用参数

    auto st = dict_.reload(dict_txt_path_);
    utils::printLine(st == status::Admin::Ok ? "ok" : "reload failed");
}

/// .num —— 词条总数。
void Server::doNum(const proto::Msg& msg) {
    (void)msg;

    utils::printLine(std::to_string(dict_.count()) + " entries");
}