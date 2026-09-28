#!/bin/bash
# ============================================================
#  run_integration.sh — 跑 integration/ 下的 it_*.sh
# ============================================================

set -e                          # 任一命令失败立即退出
cd "$(dirname "$0")/../.."      # 切到项目根

# 遍历 tests/integration/ 下所有 it_*.sh，逐个运行
shopt -s nullglob               # 无匹配时展开为空
for it in tests/integration/it_*.sh; do
    echo "=== running $it ==="
    ./"$it"                     # 执行集成测试脚本
done

echo "INTEGRATION TESTS PASSED"