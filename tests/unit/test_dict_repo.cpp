/// test_dict_repo.cpp — DictRepo 冒烟测试

#include "db/dict_repo.hpp"

#include <cassert>
#include <cstdio>       // std::remove
#include <fstream>      // std::ofstream
#include <iostream>
#include <string>
#include <vector>

int main() {
    // ==================== 1. 准备测试词库文件 ====================
    // 格式：TSV 三段 "<word>\t<pos>\t<mean>"，pos / mean 不重复。
    const char* test_txt = "/tmp/netdict_dict_test.txt";
    {
        std::ofstream f(test_txt);
        assert(f);
        // 故意造：pos 空、空行、坏行（缺 Tab）
        f << "apple\tn.\t苹果\n";
        f << "book\tn.\t书\n";
        f << "cat\t\t猫\n";                 // pos 空（两个连续 Tab）
        f << "\n";                          // 空行
        f << "broken\n";                    // 坏行：无 Tab，跳过
        f << "dog\tn.\t狗\n";
    }

    // ==================== 2. 打开内存库并导入 ====================
    DictRepo repo(":memory:");

    assert(repo.count() == 0);

    // 导入：4 条有效（apple / book / cat / dog）
    bool ok = repo.initFromFile(test_txt);
    assert(ok);
    assert(repo.count() == 4);

    // ==================== 3. 重复导入应跳过 ====================
    assert(repo.initFromFile(test_txt));
    assert(repo.count() == 4);

    // ==================== 4. query 命中（带 pos） ====================
    {
        std::vector<Meaning> out;
        auto s = repo.query("apple", out);

        assert(s == status::Query::Ok);
        assert(out.size() == 1);
        assert(out[0].pos == "n.");
        assert(out[0].mean == "苹果");
    }

    // ==================== 5. query：pos 为空 ====================
    {
        std::vector<Meaning> out;
        auto s = repo.query("cat", out);

        assert(s == status::Query::Ok);
        assert(out.size() == 1);
        assert(out[0].pos.empty());
        assert(out[0].mean == "猫");
    }

    // ==================== 6. query 未命中 ====================
    {
        std::vector<Meaning> out;
        auto s = repo.query("notexist", out);

        assert(s == status::Query::NotFound);
        assert(out.empty());
    }

    // ==================== 7. query 覆盖多义（同词多行） ====================
    {
        std::ofstream f(test_txt, std::ios::app);
        f << "apple\tn.\t苹果树\n";         // 追加第二条 apple
    }
    DictRepo repo2(":memory:");
    assert(repo2.initFromFile(test_txt));
    assert(repo2.count() == 5);              // 4 + 1

    {
        std::vector<Meaning> out;
        auto s = repo2.query("apple", out);
        assert(s == status::Query::Ok);
        assert(out.size() == 2);             // apple 两条释义
        assert(out[0].mean != out[1].mean);  // 释义不同
    }

    // ==================== 8. list 管理接口 ====================
    {
        std::vector<DictEntry> out;
        auto s = repo.list("a%", out);       // a 开头
        assert(s == status::Admin::Ok);
        assert(out.size() == 1);
        assert(out[0].word == "apple");
        assert(out[0].pos == "n.");
        assert(out[0].mean == "苹果");
    }

    // ==================== 9. add / del / update ====================
    {
        // add
        assert(repo.add("fox", Meaning{"n.", "狐狸"}) == status::Admin::Ok);

        std::vector<Meaning> out;
        assert(repo.query("fox", out) == status::Query::Ok);
        assert(out.size() == 1);
        assert(out[0].pos == "n.");
        assert(out[0].mean == "狐狸");

        // update（改 mean）
        assert(repo.update("fox", Meaning{"n.", "狐狸（改）"})
                   == status::Admin::Ok);
        assert(repo.query("fox", out) == status::Query::Ok);
        assert(out[0].mean == "狐狸（改）");

        // del
        assert(repo.del("fox") == status::Admin::Ok);
        assert(repo.query("fox", out) == status::Query::NotFound);

        // del 不存在的词 → NotFound
        assert(repo.del("nonexist") == status::Admin::NotFound);
    }

    // ==================== 10. reload ====================
    {
        assert(repo.reload(test_txt) == status::Admin::Ok);
        assert(repo.count() == 5);           // test_txt 里 5 条
    }

    // ==================== 清理 ====================
    std::remove(test_txt);

    std::cout << "[OK] dict_repo\n";
    std::cout << "ALL OK\n";
    
    return 0;
}