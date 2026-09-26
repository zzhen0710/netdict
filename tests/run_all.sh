#!/bin/bash
# run_all.sh — 编译并运行当前目录下所有 test_*.cpp
#
# 用法：
#   chmod +x run_all.sh   # 第一次给执行权限
#   ./run_all.sh          # 运行
#
# 行为：
#   逐个编译 test_*.cpp 到 /tmp/<name>，运行之；
#   任一失败立即退出；全部成功打印 ALL TESTS PASSED。

set -e   # 任一命令返回非 0（失败）立即退出，避免错误被掩盖

# 切到脚本所在目录，保证从任何位置调用，下面的相对路径都对
cd "$(dirname "$0")"

# 遍历所有测试源文件（glob 匹配 test_*.cpp）
for src in test_*.cpp; do
    name="${src%.cpp}"          # 去掉 .cpp 后缀，作为可执行文件名
    echo "=== building $name ==="

    # 编译：C++17，开警告，头文件在 ../include，链接 sqlite3，输出到 /tmp
    g++ -std=c++17 -Wall -Wextra -I../include "$src" -lsqlite3 -o "/tmp/$name"

    echo "=== running  $name ==="
    "/tmp/$name"                # 运行；测试失败（返回非 0）会因 set -e 终止脚本
done

echo "ALL TESTS PASSED"         # 能走到这里说明所有测试都返回 0