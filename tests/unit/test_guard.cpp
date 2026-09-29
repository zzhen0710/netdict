/// test_guard.cpp — Guard 三件套冒烟测试

#include "common/guard/fd_guard.hpp"
#include "common/guard/db_guard.hpp"

#include <cassert>       // assert
#include <cerrno>        // errno, EBADF
#include <fcntl.h>       // open, O_RDWR, O_CREAT, fcntl, F_GETFD
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

        // 移动赋值（目标空）：目标接管，源置 -1
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

    std::cout << "[OK] FdGuard 基本\n";

    // ==================== 2. FdGuard 移动赋值（目标已有资源） ====================
    // 验证：移动赋值会先 close 目标旧 fd，再接管源 fd
    {
        int fd_a = open("/tmp/netdict_test_fd_a.txt", O_RDWR | O_CREAT, 0644);
        int fd_b = open("/tmp/netdict_test_fd_b.txt", O_RDWR | O_CREAT, 0644);
        assert(fd_a >= 0 && fd_b >= 0);

        FdGuard a(fd_a);
        FdGuard b(fd_b);

        // b = move(a)：b 应先 close(fd_b)，再接管 fd_a
        b = std::move(a);

        assert(a.get() == -1);           // 源置空
        assert(b.get() == fd_a);         // 目标接管 fd_a

        // fd_b 应已被 close：fcntl 查不到 → EBADF
        errno = 0;
        assert(fcntl(fd_b, F_GETFD) == -1 && errno == EBADF);
    }
    // 出作用域：b 析构 close(fd_a)

    std::cout << "[OK] FdGuard 移动赋值覆盖旧资源\n";

    // ==================== 3. FdGuard 自赋值 ====================
    // 验证：a = move(a) 不会 close 自己、不会 UB
    {
        int fd = open("/tmp/netdict_test_fd_self.txt", O_RDWR | O_CREAT, 0644);
        assert(fd >= 0);

        FdGuard a(fd);
        // 自我移动赋值：内部有 this != &other 检查，应安全无操作
        FdGuard& ref = a;
        a = std::move(ref);
        assert(a.get() == fd);           // 仍持有原 fd，没被 close

        // fd 仍有效：fcntl 能查到
        assert(fcntl(fd, F_GETFD) != -1);
    }
    // 出作用域：a 析构 close(fd)

    std::cout << "[OK] FdGuard 自赋值\n";

    // ==================== 4. FdGuard 析构真的 close ====================
    // 验证：析构后 fd 不可用（EBADF）
    {
        int fd = open("/tmp/netdict_test_fd_dtor.txt", O_RDWR | O_CREAT, 0644);
        assert(fd >= 0);
        {
            FdGuard g(fd);
            assert(fcntl(fd, F_GETFD) != -1);   // 存活期间有效
        }
        // g 析构：应 close(fd)
        errno = 0;
        assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
    }

    std::cout << "[OK] FdGuard 析构 close\n";

    // ==================== 5. FdGuard 默认构造 ====================
    // 默认构造 = 空守卫（fd_ = -1），析构不做任何事
    {
        FdGuard g;                       // 默认构造
        assert(g.get() == -1);
    }
    // g 析构：fd_ = -1，不 close，无副作用

    std::cout << "[OK] FdGuard 默认构造\n";

    // ==================== 6. DbGuard + StmtGuard ====================
    {
        DbGuard db(":memory:");          // 内存库，不落盘
        assert(db.get() != nullptr);

        // ---- 6.1 验证 StmtGuard 析构会 finalize ----
        {
            StmtGuard s(db.get(), "create table t(x int)");
            assert(sqlite3_step(s.get()) == SQLITE_DONE);

            // 此时 db 上应有 1 个未 finalize 的 stmt
            assert(sqlite3_next_stmt(db.get(), nullptr) != nullptr);
        }
        // 出了作用域，s 应已 finalize，无 stmt 了
        assert(sqlite3_next_stmt(db.get(), nullptr) == nullptr);

        // ---- 6.2 插入数据 ----
        {
            StmtGuard ins(db.get(), "insert into t values(?)");
            sqlite3_bind_int(ins.get(), 1, 42);
            assert(sqlite3_step(ins.get()) == SQLITE_DONE);
        }

        // ---- 6.3 查询数据 ----
        {
            StmtGuard sel(db.get(), "select x from t");
            assert(sqlite3_step(sel.get()) == SQLITE_ROW);
            assert(sqlite3_column_int(sel.get(), 0) == 42);
        }

        // ---- 6.4 StmtGuard 移动 ----
        {
            StmtGuard s1(db.get(), "select x from t");
            StmtGuard s2(std::move(s1));
            assert(s1.get() == nullptr);        // 源置空
            assert(s2.get() != nullptr);        // 目标接管
            assert(sqlite3_step(s2.get()) == SQLITE_ROW);
        }

        // ---- 6.5 构造失败应抛异常 ----
        bool thrown = false;
        try {
            StmtGuard bad(db.get(), "this is not valid sql");
        } catch (const std::exception&) {
            thrown = true;
        }
        assert(thrown);                         // 确认抛了
    }   // db 析构，close

    std::cout << "[OK] DbGuard + StmtGuard\n";

    // ==================== 7. DbGuard 移动 ====================
    {
        DbGuard a(":memory:");
        sqlite3* raw = a.get();
        assert(raw != nullptr);

        // 移动构造
        DbGuard b(std::move(a));
        assert(a.get() == nullptr);          // 源置空
        assert(b.get() == raw);              // 目标接管

        // 移动赋值（目标已有库，会先 close 旧库）。
        // 接管后新库能正常用。
        DbGuard c(":memory:");
        assert(c.get() != nullptr);
        c = std::move(b);
        assert(b.get() == nullptr);
        assert(c.get() == raw);
        {
            StmtGuard s(c.get(), "select 1");
            assert(sqlite3_step(s.get()) == SQLITE_ROW);
        }
    }
    // 出作用域：b 空（不 close），c 析构 close

    std::cout << "[OK] DbGuard 移动\n";

    // ==================== 8. DbGuard 默认构造 ====================
    {
        DbGuard g;                       // 默认构造 = 空守卫
        assert(g.get() == nullptr);
    }
    // g 析构：pdb_ = nullptr，不 close

    std::cout << "[OK] DbGuard 默认构造\n";

    std::cout << "ALL OK\n";

    return 0;
}