/// test_line_editor.cpp — LineEditor / EditorHistory 冒烟测试

#include "common/line_editor.hpp"

#include <cassert>
#include <cstdlib>       // std::setenv, std::unsetenv, mkdtemp
#include <fstream>       // std::ifstream
#include <iostream>
#include <string>
#include <unistd.h>      // access, unlink, rmdir

int main() {
    // ==================== 1. EditorHistory：构造 / add / 析构 Save ====================
    {
        // 建唯一临时 HOME，隔离真实 ~
        char tmpl[] = "/tmp/netdict_hist_XXXXXX";
        char* tmp_home = mkdtemp(tmpl);
        assert(tmp_home != nullptr);

        setenv("HOME", tmp_home, 1);

        const char* file_name = "/.netdict_test_history";
        std::string full_path = std::string(tmp_home) + file_name;

        // 清理可能存在的旧文件
        unlink(full_path.c_str());

        // 构造 → add 两行 → 析构（Save）
        {
            EditorHistory hist(100, file_name);
            hist.add("first");
            hist.add("second");
        }

        // 析构后文件应存在
        assert(access(full_path.c_str(), F_OK) == 0);

        // 内容应有两行
        std::ifstream f(full_path);
        assert(f);
        std::string l1, l2;
        std::getline(f, l1);
        std::getline(f, l2);
        assert(l1 == "first");
        assert(l2 == "second");

        // 清理
        unlink(full_path.c_str());
        rmdir(tmp_home);
        unsetenv("HOME");
    }
    std::cout << "[OK] EditorHistory 构造/add/析构 Save\n";

    // ==================== 2. EditorHistory：文件不存在时构造不报错 ====================
    // Load 文件不存在返回 -1，视为空历史，不抛
    {
        // 指向一个不存在的文件路径（HOME 已 unset，resolvePath 兜底 "."）
        EditorHistory hist(100, "/no/such/path/nonexistent_history_file");
        hist.add("x");   // 仍可加，不崩
    }
    std::cout << "[OK] EditorHistory 文件不存在不报错\n";

    // ==================== 3. LineEditor：无效 fd 走非 tty 分支 ====================
    // 无效 fd：isatty 假 → 非 tty 分支（不调 linenoiseEditStart）→ 构造成功。
    // 非 tty 时 feed() 走 std::getline（不碰 linenoise）。
    {
        LineEditor ed(1314520, 1314520, "prompt> ");   // 不抛
        // 不调 feed（会阻塞 getline 读 stdin）；只验证构造成功、可析构。
    }
    std::cout << "[OK] LineEditor 无效 fd（非 tty 分支）不抛\n";

    std::cout << "ALL OK\n";
    return 0;
}