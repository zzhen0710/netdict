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
    const char* test_txt = "/tmp/netdict_dict_test.txt";
    {
        std::ofstream f(test_txt);
        assert(f);
        // 故意造：多空格、行首空格、行尾空格、空行、坏行（无空格）
        f << "apple   n.苹果\n";
        f << "book   n.书\n";
        f << "  cat   n.猫  \n";   // 行首/行尾空格
        f << "\n";                  // 空行
        f << "broken\n";            // 无空格：应跳过
        f << "dog   n.狗\n";
    }

    // ==================== 2. 打开内存库并导入 ====================
    DictRepo repo(":memory:");

    // 初始为空
    assert(repo.count() == 0);

    // 导入：4 条有效（apple / book / cat / dog）
    bool ok = repo.initFromFile(test_txt);
    assert(ok);
    assert(repo.count() == 4);

    // ==================== 3. 重复导入应跳过 ====================
    // 表非空 → initFromFile 直接返回 true，不重复导入
    assert(repo.initFromFile(test_txt));
    assert(repo.count() == 4);

    // ==================== 4. query 命中 ====================
    {
        std::vector<Meaning> out;
        auto s = repo.query("apple", out);

        assert(s == stat::Query::Ok);
        assert(out.size() == 1);
        assert(out[0].text == "n.苹果");
        assert(out[0].rowid > 0);          // rowid 是正整数
    }

    // ==================== 5. query 行首/行尾空格已被裁剪 ====================
    {
        std::vector<Meaning> out;
        auto s = repo.query("cat", out);

        assert(s == stat::Query::Ok);
        assert(out.size() == 1);
        assert(out[0].text == "n.猫");     // 无前导/尾随空格
    }

    // ==================== 6. query 未命中 ====================
    {
        std::vector<Meaning> out;
        auto s = repo.query("notexist", out);

        assert(s == stat::Query::NotFound);
        assert(out.empty());
    }

    // ==================== 7. query 覆盖多义（同词多行） ====================
    // 造一个同词多义：往文件追加，清库重导
    {
        std::ofstream f(test_txt, std::ios::app);
        f << "apple   n.苹果树\n";         // 追加第二条 apple
    }
    // 用新库重导（旧库非空不会重导）
    DictRepo repo2(":memory:");
    assert(repo2.initFromFile(test_txt));
    assert(repo2.count() == 5);            // 原来 4 条 + 新增 1 条

    {
        std::vector<Meaning> out;
        auto s = repo2.query("apple", out);
        assert(s == stat::Query::Ok);
        assert(out.size() == 2);           // apple 有两条释义
        // rowid 各不相同
        assert(out[0].rowid != out[1].rowid);
    }

    // ==================== 清理 ====================
    std::remove(test_txt);

    std::cout << "[OK] dict_repo\n";
    std::cout << "ALL OK\n";
    return 0;
}