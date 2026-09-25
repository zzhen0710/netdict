/// test_guard.cpp — Guard 三件套冒烟测试

#include "common/guard/fd_guard.hpp"
#include "common/guard/db_guard.hpp"

#include <cassert>       // assert
#include <fcntl.h>       // open, O_RDWR, O_CREAT
#include <iostream>      // std::cout
#include <sqlite3.h>     // sqlite3_next_stmt
#include <unistd.h>      // close
#include <utility>       // std::move

int main() {
    // ==================== 1. FdGuard ====================
    {
        int fd = open("/tmp/netdict_test_fd.txt", O_RDWR | O_CREAT, 0644);
        assert(fd >= 0);

        FdGuard fg(fd);
        assert(fg.get() == fd);          // 构造后持有原 fd

        // 移动构造：目标接管，源置 -1
        FdGuard fg2(std::move(fg));
        assert(fg.get() == -1);          // 源被置空
        assert(fg2.get() == fd);         // 目标接管

        // 移动赋值
        FdGuard fg3(-1);                 // 空接管
        fg3 = std::move(fg2);
        assert(fg2.get() == -1);
        assert(fg3.get() == fd);

        // release 放弃所有权
        int raw = fg3.release();
        assert(raw == fd);
        assert(fg3.get() == -1);
        ::close(raw);                     // 调全局的 close，自己负责关
    }   // fg、fg2、fg3 析构，fd 已 release 或已 close

    std::cout << "[OK] FdGuard\n";

    // ==================== 2. DbGuard + StmtGuard ====================
    {
        DbGuard db(":memory:");          // 内存库，不落盘
        assert(db.get() != nullptr);

        // ---- 2.1 验证 StmtGuard 析构会 finalize ----
        {
            StmtGuard s(db.get(), "create table t(x int)");
            assert(sqlite3_step(s.get()) == SQLITE_DONE);

            // 此时 db 上应有 1 个未 finalize 的 stmt
            assert(sqlite3_next_stmt(db.get(), nullptr) != nullptr);
        }
        // 出了作用域，s 应已 finalize，无 stmt 了
        assert(sqlite3_next_stmt(db.get(), nullptr) == nullptr);

        // ---- 2.2 插入数据 ----
        {
            StmtGuard ins(db.get(), "insert into t values(?)");
            sqlite3_bind_int(ins.get(), 1, 42);
            assert(sqlite3_step(ins.get()) == SQLITE_DONE);
        }

        // ---- 2.3 查询数据 ----
        {
            StmtGuard sel(db.get(), "select x from t");
            assert(sqlite3_step(sel.get()) == SQLITE_ROW);
            assert(sqlite3_column_int(sel.get(), 0) == 42);
        }

        // ---- 2.4 StmtGuard 移动 ----
        {
            StmtGuard s1(db.get(), "select x from t");
            StmtGuard s2(std::move(s1));
            assert(s1.get() == nullptr);        // 源置空
            assert(s2.get() != nullptr);        // 目标接管
            assert(sqlite3_step(s2.get()) == SQLITE_ROW);
        }

        // ---- 2.5 构造失败应抛异常 ----
        bool thrown = false;
        try {
            StmtGuard bad(db.get(), "this is not valid sql");
        } catch (const std::exception&) {
            thrown = true;
        }
        assert(thrown);                         // 确认抛了
    }   // db 析构，close

    std::cout << "[OK] DbGuard + StmtGuard\n";

    std::cout << "ALL OK\n";
    return 0;
}