/// test_usr_repo.cpp — UsrRepo 冒烟测试

#include "db/usr_repo.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

int main() {
    // ==================== 0. 内存库 ====================
    UsrRepo repo(":memory:");

    // ==================== 1. reg ====================
    {
        // 首次注册成功
        assert(repo.reg("alice", "123") == stat::UsrOp::Ok);
        // 重复注册 → Exists
        assert(repo.reg("alice", "456") == stat::UsrOp::Exists);
        // 另一个用户
        assert(repo.reg("bob", "abc") == stat::UsrOp::Ok);
    }
    std::cout << "[OK] reg\n";

    // ==================== 2. login / logout ====================
    {
        // 用户不存在
        assert(repo.login("nobody", "x") == stat::UsrOp::NotFound);

        // reg 后 stage 已是 Connected，先登出再测
        assert(repo.logout("alice") == stat::UsrOp::Ok);

        // 密码错
        assert(repo.login("alice", "wrong") == stat::UsrOp::WrongPwd);

        // 正确登录（stage: Disconnected → Connected）
        assert(repo.login("alice", "123") == stat::UsrOp::Ok);

        // 已在线再登录 → Online
        assert(repo.login("alice", "123") == stat::UsrOp::Online);

        // 登出（stage → Disconnected）
        assert(repo.logout("alice") == stat::UsrOp::Ok);

        // 隐式验证 stage 已回 Disconnected：
        //   若 stage 仍 Connected，下面 login 会返回 Online
        //   真正"看 stage"由 cmd 的 .stat 命令负责
        assert(repo.login("alice", "123") == stat::UsrOp::Ok);

        // 再登出 → 成功
        assert(repo.logout("alice") == stat::UsrOp::Ok);

        // 已离线再登出 → 无行被改 → NotFound
        assert(repo.logout("alice") == stat::UsrOp::NotFound);

        // 用户不存在 → 无行被改 → NotFound
        assert(repo.logout("nobody") == stat::UsrOp::NotFound);
    }
    std::cout << "[OK] login/logout\n";

    // ==================== 3. 历史 ====================
    {
        // 写三条
        assert(repo.addHistory("alice", {"apple", "n.苹果", "2026-01-01 10:00:00"}));
        assert(repo.addHistory("alice", {"book",  "n.书",   "2026-01-01 10:01:00"}));
        assert(repo.addHistory("alice", {"cat",   "n.猫",   "2026-01-01 10:02:00"}));
        // bob 一条，用于验证按 name 隔离
        assert(repo.addHistory("bob", {"dog", "n.狗", "2026-01-01 11:00:00"}));

        // 取最近 2 条：按 rowid 倒序 → cat / book
        std::vector<HistoryEntry> out;
        assert(repo.getHistory("alice", 2, out));
        assert(out.size() == 2);
        assert(out[0].word == "cat");
        assert(out[1].word == "book");

        // 取 10 条：只有 3 条
        assert(repo.getHistory("alice", 10, out));
        assert(out.size() == 3);

        // bob 只看到自己的 1 条
        assert(repo.getHistory("bob", 10, out));
        assert(out.size() == 1);
        assert(out[0].word == "dog");

        // 无历史用户返回空
        assert(repo.getHistory("nobody", 10, out));
        assert(out.empty());
    }
    std::cout << "[OK] history\n";

    // ==================== 4. star / unstar / getStars ====================
    {
        // 首次收藏
        assert(repo.star("alice", "apple") == stat::Query::Ok);
        // 重复收藏 → Starred
        assert(repo.star("alice", "apple") == stat::Query::Starred);
        // 再收藏两个
        assert(repo.star("alice", "cat")   == stat::Query::Ok);
        assert(repo.star("alice", "book")  == stat::Query::Ok);

        // 字母序：apple / book / cat
        std::vector<std::string> stars;
        auto s = repo.getStars("alice", 10, stars);
        assert(s == stat::Query::Ok);
        assert(stars.size() == 3);
        assert(stars[0] == "apple");
        assert(stars[1] == "book");
        assert(stars[2] == "cat");

        // limit 生效：取前 2
        assert(repo.getStars("alice", 2, stars) == stat::Query::Ok);
        assert(stars.size() == 2);
        assert(stars[0] == "apple");
        assert(stars[1] == "book");

        // 取消收藏
        assert(repo.unstar("alice", "apple") == stat::Query::Ok);
        // 再取消 → 本来就没收藏
        assert(repo.unstar("alice", "apple") == stat::Query::Unstarred);

        // 剩 2 条
        assert(repo.getStars("alice", 10, stars) == stat::Query::Ok);
        assert(stars.size() == 2);

        // bob 无收藏
        assert(repo.getStars("bob", 10, stars) == stat::Query::Ok);
        assert(stars.empty());
    }
    std::cout << "[OK] star/unstar/getStars\n";

    std::cout << "ALL OK\n";
    return 0;
}