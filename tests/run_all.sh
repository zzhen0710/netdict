#!/bin/bash
# ============================================================
#  run_all.sh — 跑 tests/ 下全部测试（单元 + 集成）
# ============================================================
#
#  首次使用：
#    cd tests
#    chmod +x run_all.sh unit/*.sh integration/*.sh
#    ./run_all.sh
#
#  用法：（tests 目录下）
#    ./run_all.sh              # 全部
#    ./run_all.sh unit         # 只单元
#    ./run_all.sh integration  # 只集成
# ============================================================

set -e                          # 任一命令失败立即退出
cd "$(dirname "$0")/.."         # 切到项目根

# 按第一个参数选择跑哪类测试；默认 all
case "${1:-all}" in
    all)
        ./tests/unit/run_unit.sh               # 跑单元测试
        ./tests/integration/run_integration.sh # 跑集成测试
        echo "ALL TESTS PASSED"
        ;;
    unit)
        ./tests/unit/run_unit.sh               # 只跑单元
        ;;
    integration)
        ./tests/integration/run_integration.sh # 只跑集成
        ;;
    *)
        echo "用法: $0 [all|unit|integration]"
        exit 1                                 # 参数不合法，报用法退出
        ;;
esac