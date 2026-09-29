#!/bin/bash
# ============================================================
#  run_unit.sh — 编译并运行 unit/ 下的单元测试
# ============================================================
#
#  用法：
#    ./run_unit.sh              # 全部
#    ./run_unit.sh test_proto   # 只跑 test_proto
#    ./run_unit.sh list         # 列出所有
#
#  约定：cd 到项目根，路径统一相对根。
# ============================================================

set -e                          # 任一命令失败立即退出
cd "$(dirname "$0")/../.."      # 切到项目根

# 测试名 → 源文件列表（相对项目根）
declare -A TESTS=(
    [test_guard]="tests/unit/test_guard.cpp"            # 无依赖，内容都在 hpp
    [test_proto]="tests/unit/test_proto.cpp src/common/proto.cpp"
    [test_dict_repo]="tests/unit/test_dict_repo.cpp src/db/dict_repo.cpp"
    [test_usr_repo]="tests/unit/test_usr_repo.cpp src/db/usr_repo.cpp"
    [test_thread_pool]="tests/unit/test_thread_pool.cpp src/ser/thread_pool.cpp"
    [test_logger]="tests/unit/test_logger.cpp src/common/logger.cpp"
    [test_utils]="tests/unit/test_utils.cpp src/common/utils.cpp"
    [test_net]="tests/unit/test_net.cpp src/common/net.cpp"
    [test_line_editor]="tests/unit/test_line_editor.cpp src/common/line_editor.cpp /tmp/linenoise.o"
)

# 预编译第三方 C 源：g++ 会把 .c 当 C++ 编，需 gcc 单独编；
if [ ! -f /tmp/linenoise.o ] || [ third_party/linenoise.c -nt /tmp/linenoise.o ]; then
    gcc -c third_party/linenoise.c -Ithird_party -o /tmp/linenoise.o
fi

# 编译并运行单个测试
run_one() {
    local name=$1
    local srcs=(${TESTS[$name]})        # 按空格拆成源文件数组

    echo "=== building $name ==="
    # 编译：头文件、第三方库、源文件、链接库、输出
    g++ -std=c++17 -Wall -Wextra \
        -Iinclude \
        -Ithird_party \
        "${srcs[@]}" \
        -lsqlite3 -pthread \
        -o "/tmp/$name"

    echo "=== running  $name ==="
    "/tmp/$name"                        # 运行测试
}

# 按参数决定跑全部、列清单、还是单个
if [ $# -eq 0 ]; then
    for name in "${!TESTS[@]}"; do run_one "$name"; done
    echo "UNIT TESTS PASSED"
elif [ "$1" = "list" ]; then
    for name in "${!TESTS[@]}"; do echo "$name"; done
else
    if [ -z "${TESTS[$1]}" ]; then
        echo "未知测试: $1"
        echo "可用: $(printf '%s ' "${!TESTS[@]}")"
        exit 1
    fi
    run_one "$1"
fi