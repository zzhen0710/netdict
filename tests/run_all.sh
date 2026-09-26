#!/bin/bash
# ============================================================
#  run_all.sh — 编译并运行 tests/ 下的单元测试
# ============================================================
#
#  用法：
#    ./run_all.sh              # 跑全部测试
#    ./run_all.sh test_proto   # 只跑 test_proto
#    ./run_all.sh list         # 列出所有测试名
#
#  行为：
#    每个测试编译到 /tmp/<name>，运行之；
#    任一失败立即退出；全部成功打印 ALL TESTS PASSED。
#
#  加新测试：
#    在下面 TESTS 关联数组里加一行即可。
# ============================================================

set -e   # 任一命令返回非 0 立即退出

# 切到脚本所在目录，保证从任何位置调用，相对路径都对
cd "$(dirname "$0")"

# ------------------------------------------------------------
#  测试列表：<测试名> → <源文件列表（空格分隔）>
# ------------------------------------------------------------
#  - 测试名 = 可执行文件名（放到 /tmp/<name>）
#  - 源文件 = 测试文件 + 它依赖的 .cpp（相对本目录）
#  - 只依赖头文件的测试（如 test_guard）只写自己
# ------------------------------------------------------------
declare -A TESTS=(
    [test_guard]="test_guard.cpp"
    [test_proto]="test_proto.cpp ../src/common/proto.cpp"
    [test_dict_repo]="test_dict_repo.cpp ../src/db/dict_repo.cpp"
    [test_usr_repo]="test_usr_repo.cpp ../src/db/usr_repo.cpp"
)

# ------------------------------------------------------------
#  编译并运行单个测试
#  参数：<测试名>
# ------------------------------------------------------------
run_one() {
    local name=$1
    local srcs=(${TESTS[$name]})         # 按空格拆成数组
    echo "=== building $name ==="
    g++ -std=c++17 -Wall -Wextra \
        -I../include \
        "${srcs[@]}" \
        -lsqlite3 \
        -o "/tmp/$name"
    echo "=== running  $name ==="
    "/tmp/$name"
}

# ------------------------------------------------------------
#  入口
# ------------------------------------------------------------
if [ $# -eq 0 ]; then
    # 无参数：跑全部
    for name in "${!TESTS[@]}"; do
        run_one "$name"
    done
    echo "ALL TESTS PASSED"

elif [ "$1" = "list" ]; then
    # list：列出所有测试名
    for name in "${!TESTS[@]}"; do
        echo "$name"
    done

else
    # 指定测试名
    if [ -z "${TESTS[$1]}" ]; then
        echo "未知测试: $1"
        echo "可用: $(printf '%s ' "${!TESTS[@]}")"
        exit 1
    fi
    run_one "$1"
fi