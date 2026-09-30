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
        assert(repo.reg("alice", "123") == status::UsrOp::Ok);
        assert(repo.reg("alice", "456") == status::UsrOp::Exists);
        assert(repo.reg("bob", "abc")   == status::UsrOp::Ok);
    }
    std::cout << "[OK] reg\n";

    // ==================== 1.5 exists ====================
    {
        assert(repo.exists("alice")  == true);    // 刚注册
        assert(repo.exists("bob")    == true);    // 刚注册
        assert(repo.exists("nobody") == false);   // 不存在
    }
    std::cout << "[OK] exists\n";

    // ==================== 2. login ====================
    {
        assert(repo.login("nobody", "x")   == status::UsrOp::NotFound);
        assert(repo.login("alice", "wrong") == status::UsrOp::WrongPwd);
        assert(repo.login("alice", "123")   == status::UsrOp::Ok);
    }
    std::cout << "[OK] login\n";

    // ==================== 3. 历史 ====================
    {
        // 一个词多条释义（vector<Meaning>）
        std::vector<Meaning> apple_defs = {
            {"n.", "苹果"},
        };
        std::vector<Meaning> like_defs = {
            {"v.",    "喜欢"},
            {"prep.", "像"},
        };

        // 写历史
        assert(repo.addHistory("alice", "apple", apple_defs, "2026-01-01 10:00:00"));
        assert(repo.addHistory("alice", "like",  like_defs,  "2026-01-01 10:01:00"));
        assert(repo.addHistory("bob",   "dog",
                               {{"n.", "狗"}}, "2026-01-01 11:00:00"));

        // 取 alice 最近 10 条：like 2 行 + apple 1 行 = 3 行
        std::vector<HistoryEntry> out;
        assert(repo.getHistory("alice", 10, out));
        assert(out.size() == 3);          // like(2) + apple(1)
        // 按 rowid desc：like 的两行先（10:01），再 apple
        assert(out[0].word == "like");
        assert(out[1].word == "like");
        assert(out[2].word == "apple");

        // bob 只有 1 条
        assert(repo.getHistory("bob", 10, out));
        assert(out.size() == 1);
        assert(out[0].word == "dog");

        // 无历史用户
        assert(repo.getHistory("nobody", 10, out));
        assert(out.empty());
    }
    std::cout << "[OK] history\n";

    // ==================== 4. star / unstar / getStars ====================
    {
        std::vector<Meaning> apple_defs = {{"n.", "苹果"}};
        std::vector<Meaning> like_defs  = {{"v.", "喜欢"}, {"prep.", "像"}};

        // 首次收藏 apple
        assert(repo.star("alice", "apple", apple_defs, "2026-01-01 10:00:00")
                   == status::Query::Ok);
        // 重复收藏 apple → Starred
        assert(repo.star("alice", "apple", apple_defs, "2026-01-01 10:00:00")
                   == status::Query::Starred);

        // 收藏 like（2 释义 → star 表 2 行）
        assert(repo.star("alice", "like", like_defs, "2026-01-01 10:01:00")
                   == status::Query::Ok);

        // 字母序：apple / like；like 有 2 行
        std::vector<StarEntry> stars;
        assert(repo.getStars("alice", 10, stars) == status::Query::Ok);
        assert(stars.size() == 3);        // apple(1) + like(2)
        assert(stars[0].word == "apple");
        assert(stars[1].word == "like");
        assert(stars[2].word == "like");

        // limit：取前 2 个词（apple 1 行 + like 2 行 = 3 行）
        assert(repo.getStars("alice", 2, stars) == status::Query::Ok);
        assert(stars.size() == 3);
        assert(stars[0].word == "apple");
        assert(stars[1].word == "like");
        assert(stars[2].word == "like");

        // 取消收藏 apple
        assert(repo.unstar("alice", "apple") == status::Query::Ok);
        // 再取消 → Unstarred
        assert(repo.unstar("alice", "apple") == status::Query::Unstarred);

        // 剩 like 的 2 行
        assert(repo.getStars("alice", 10, stars) == status::Query::Ok);
        assert(stars.size() == 2);
        assert(stars[0].word == "like");

        // bob 无收藏
        assert(repo.getStars("bob", 10, stars) == status::Query::Ok);
        assert(stars.empty());
    }
    std::cout << "[OK] star/unstar/getStars\n";

    std::cout << "ALL OK\n";
    
    return 0;
}