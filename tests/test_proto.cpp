/// test_proto.cpp — proto 编解码与响应函数冒烟测试

#include "common/proto.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <variant>    // std::variant, holds_alternative, get

int main() {
    // ==================== 1. encode ====================
    {
        // 单参数：query + apple
        assert(proto::encode({proto::UsrCmd::Dict::Query, {"apple"}}) == "query apple");

        // 无参数：logout
        assert(proto::encode({proto::UsrCmd::Ctrl::Logout, {}}) == "logout");

        // 多参数：add + apple + n.苹果
        assert(proto::encode({proto::SysCmd::Dict::Add, {"apple", "n.苹果"}}) == "add apple n.苹果");

        // 控制命令：log + info
        assert(proto::encode({proto::SysCmd::Ctrl::Log, {"info"}}) == "log info");
    }
    std::cout << "[OK] encode\n";

    // ==================== 2. decodeUsr ====================
    {
        // --- 2.1 用户字典命令 ---
        auto m1 = proto::decodeUsr("query apple");
        assert(m1.has_value());                                         
        assert(std::holds_alternative<proto::UsrCmd::Dict>(m1->cmd));   // 类型正确
        assert(std::get<proto::UsrCmd::Dict>(m1->cmd) == proto::UsrCmd::Dict::Query);  // 值正确
        assert(m1->args.size() == 1);
        assert(m1->args[0] == "apple");

        // --- 2.2 用户控制命令：reg 双参数 ---
        auto m2 = proto::decodeUsr("reg alice 123");
        assert(m2.has_value());
        assert(std::holds_alternative<proto::UsrCmd::Ctrl>(m2->cmd));
        assert(std::get<proto::UsrCmd::Ctrl>(m2->cmd) == proto::UsrCmd::Ctrl::Reg);
        assert(m2->args == std::vector<std::string>({"alice", "123"}));

        // --- 2.3 无参数命令 ---
        auto m3 = proto::decodeUsr("logout");
        assert(m3.has_value());
        assert(std::get<proto::UsrCmd::Ctrl>(m3->cmd) == proto::UsrCmd::Ctrl::Logout);
        assert(m3->args.empty());

        // --- 2.4 多空格容错 ---
        auto m4 = proto::decodeUsr("  query   apple  ");
        assert(m4.has_value());
        assert(std::get<proto::UsrCmd::Dict>(m4->cmd) == proto::UsrCmd::Dict::Query);
        assert(m4->args == std::vector<std::string>({"apple"}));

        // --- 2.5 失败场景 ---
        assert(!proto::decodeUsr("").has_value());          // 空串
        assert(!proto::decodeUsr("   ").has_value());       // 全空格
        assert(!proto::decodeUsr("foo bar").has_value());   // 未知命令
        assert(!proto::decodeUsr("list").has_value());      // SysCmd 命令，Usr 不认
    }
    std::cout << "[OK] decodeUsr\n";

    // ==================== 3. decodeSys ====================
    {
        // --- 3.1 管理端字典命令 ---
        auto m1 = proto::decodeSys("list");
        assert(m1.has_value());
        assert(std::holds_alternative<proto::SysCmd::Dict>(m1->cmd));
        assert(std::get<proto::SysCmd::Dict>(m1->cmd) == proto::SysCmd::Dict::List);
        assert(m1->args.empty());

        // --- 3.2 管理端字典：add + 双参数 ---
        auto m2 = proto::decodeSys("add apple n.苹果");
        assert(m2.has_value());
        assert(std::get<proto::SysCmd::Dict>(m2->cmd) == proto::SysCmd::Dict::Add);
        assert(m2->args == std::vector<std::string>({"apple", "n.苹果"}));

        // --- 3.3 管理端控制：history（注意是 SysCmd::Ctrl 的） ---
        auto m3 = proto::decodeSys("history");
        assert(m3.has_value());
        assert(std::holds_alternative<proto::SysCmd::Ctrl>(m3->cmd));
        assert(std::get<proto::SysCmd::Ctrl>(m3->cmd) == proto::SysCmd::Ctrl::History);

        // --- 3.4 失败场景 ---
        assert(!proto::decodeSys("").has_value());
        assert(!proto::decodeSys("foo").has_value());
        assert(!proto::decodeSys("query apple").has_value());      // UsrCmd 命令，Sys 不认
        assert(!proto::decodeSys("login alice 123").has_value());
    }
    std::cout << "[OK] decodeSys\n";

    // ==================== 4. 响应函数 ====================
    {
        // --- 4.1 isOk ---
        assert(proto::isOk("ok"));                    // 无 data 也算 ok
        assert(proto::isOk("ok apple|n.苹果"));
        assert(!proto::isOk("not_found"));
        assert(!proto::isOk("err db failure"));

        // --- 4.2 respStat ---
        assert(proto::respStat("ok apple|n.苹果") == "ok");
        assert(proto::respStat("not_found")       == "not_found");
        assert(proto::respStat("err db failure")  == "err");
        assert(proto::respStat("ok")              == "ok");

        // --- 4.3 respData ---
        assert(proto::respData("ok apple|n.苹果") == "apple|n.苹果");
        assert(proto::respData("ok")              == "");   // 无 data
        assert(proto::respData("ok ")             == "");   // 只有 "ok "
        assert(proto::respData("not_found")       == "");   // 非 ok 响应

        // --- 4.4 makeOk ---
        assert(proto::makeOk("")             == "ok");
        assert(proto::makeOk("apple|n.苹果") == "ok apple|n.苹果");

        // --- 4.5 makeErr ---
        assert(proto::makeErr("not_found")         == "not_found");
        assert(proto::makeErr("err", "db failure") == "err db failure");
        assert(proto::makeErr("exists")            == "exists");
    }
    std::cout << "[OK] response\n";

    std::cout << "ALL OK\n";
    return 0;
}