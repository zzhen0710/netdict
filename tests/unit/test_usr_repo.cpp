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

    // ==================== 2. login ====================
    {
        // 用户不存在
        assert(repo.login("nobody", "x") == stat::UsrOp::NotFound);
        // 密码错
        assert(repo.login("alice", "wrong") == stat::UsrOp::WrongPwd);
        // 正确登录
        assert(repo.login("alice", "123") == stat::UsrOp::Ok);
        // 重复登录仍 Ok（多设备；在线由 Server 的 sessions_ 管）
        assert(repo.login("alice", "123") == stat::UsrOp::Ok);
    }
    std::cout << "[OK] login\n";

    // ==================== 3. 历史 ====================
    {
        // 写三条
        assert(repo.addHistory("alice", {"apple", "n.苹果", "2026-01-01 10:00:00"}));
        assert(repo.addHistory("alice", {"book",  "n.书",   "2026-01-01 10:01:00"}));
        assert(repo.addHistory("alice", {"cat",   "n.猫",   "2026-01-01 10:02:00"}));
        // bob 一条，验证按 name 隔离
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
        assert(repo.star("alice", {"apple", "n.苹果", "2026-01-01 10:00:00"})
                   == stat::Query::Ok);
        // 重复收藏 → Starred
        assert(repo.star("alice", {"apple", "n.苹果", "2026-01-01 10:00:00"})
                   == stat::Query::Starred);
        // 再收藏两个
        assert(repo.star("alice", {"cat", "n.猫", "2026-01-01 10:01:00"})
                   == stat::Query::Ok);
        assert(repo.star("alice", {"book", "n.书", "2026-01-01 10:02:00"})
                   == stat::Query::Ok);

        // 字母序：apple / book / cat
        std::vector<StarEntry> stars;
        assert(repo.getStars("alice", 10, stars) == stat::Query::Ok);
        assert(stars.size() == 3);
        assert(stars[0].word == "apple" && stars[0].mean == "n.苹果");
        assert(stars[1].word == "book"  && stars[1].mean == "n.书");
        assert(stars[2].word == "cat"   && stars[2].mean == "n.猫");

        // limit 生效：取前 2
        assert(repo.getStars("alice", 2, stars) == stat::Query::Ok);
        assert(stars.size() == 2);
        assert(stars[0].word == "apple");
        assert(stars[1].word == "book");

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