#!/bin/bash
# ============================================================
#  it_cli_smoke.sh — cli 冒烟集成测试
# ============================================================
#
#  覆盖：
#    L1 未登录：help / query / history / star / pad / logout / quit
#    L2 账号流程：reg / 重复 reg / logout / 重复 logout /
#                login 错密码 / login 不存在 / 重复 login / 少参数
#    L3 登录后：query / history / star / unstar / pad
#
#  流程：
#    起 netdict_server（后台）→ 管道驱动 netdict_client
#    → grep 关键输出 → 退出时自动 kill 服务器
#
#  依赖：
#    项目根已 make，生成 ./netdict_server / ./netdict_client
#
#  日志：
#    服务器日志走 logs/netdict_server.log（logger::setFile），
#    不再重定向到 /tmp（脚本读 logs/ 下的文件）。
#
#  端口：13999（避开默认 13140）
# ============================================================

set -e                          # 任一命令失败立即退出
cd "$(dirname "$0")/../.."      # 切到项目根
PORT=13999
SERVER_LOG=logs/netdict_server.log

# ---------- 前置 ----------
[ -x ./netdict_server ] || { echo "缺少 server（先 make）"; exit 1; }
[ -x ./netdict_client ] || { echo "缺少 client（先 make）"; exit 1; }
mkdir -p logs                   # 确保 logs/ 存在
rm -f data/usr.db               # 删旧用户库，保证干净
rm -f "$SERVER_LOG"             # 清旧服务器日志，防残留

# ---------- 清残留 ----------
# 上次若残留 netdict_server（如卡 SIGTTIN 没退），占端口 → bind failed。
# 先杀掉，保证端口空闲。
pkill -f "netdict_server.*$PORT" 2>/dev/null || true
sleep 0.2

# ---------- 起服务器 ----------
# 服务器自己写 logs/netdict_server.log（logger::setFile）；
# stdout/stderr 重定向到 /dev/null。
# < /dev/null：stdin 不是终端 → isatty 假 → 不建管理终端（否则后台读终端收 SIGTTIN 停）。
./netdict_server 0.0.0.0 "$PORT" < /dev/null > /dev/null 2>&1 &
SER_PID=$!
trap 'kill $SER_PID 2>/dev/null || true' EXIT   # 退出时自动杀服务器

# 轮询日志，等服务器打印 "listen on"（最多 2 秒）
for _ in $(seq 1 20); do
    grep -q "listen on" "$SERVER_LOG" && break
    sleep 0.1
done
grep -q "listen on" "$SERVER_LOG" \
    || { echo "服务器未启动"; cat "$SERVER_LOG" 2>/dev/null; exit 1; }

# ---------- 辅助：跑一段 cli，返回输出 ----------
run_cli() {
    printf '%s\n' "$@" | ./netdict_client 127.0.0.1 "$PORT" 2>/dev/null
}

# ---------- 断言：$1 输出含 $2 ----------
check() {
    local out="$1" pat="$2"
    echo "$out" | grep -q "$pat" \
        || { echo "FAIL: 未匹配 [$pat]"; echo "--- 实际 ---"; echo "$out"; exit 1; }
}

# ------------ L1：未登录 ------------
# 未登录下跑一串命令，检查关键输出
OUT=$(run_cli \
    '.help' \
    '.query apple' \
    '.history' \
    '.star apple' \
    '.pad' \
    '.logout' \
    '.quit')

check "$OUT" "account"                    # help
check "$OUT" "not logged in"              # query/history/star/pad/logout 都 err
check "$OUT" "goodbye"                    # quit

echo "OK L1 (未登录)"

# ------------ L2：账号流程 ------------
# 注册、重复注册、登出、重复登出、错密码、不存在用户、重复登录、少参数
OUT=$(run_cli \
    '.reg alice 12345' \
    '.reg alice 678' \
    '.logout' \
    '.logout' \
    '.reg alice 678' \
    '.login alice wrong' \
    '.login nobody 123' \
    '.login alice 12345' \
    '.login alice 12345' \
    '.reg bob' \
    '.login bob' \
    '.logout' \
    '.login alice 12345' \
    '.quit')

check "$OUT" "welcome, alice"             # reg 成功
check "$OUT" "already logged in"          # 已登录再 reg
check "$OUT" "not logged in"              # 已登出再 logout
check "$OUT" "exists"                     # 注册已存在
check "$OUT" "wrong_pwd"                  # 密码错
check "$OUT" "not_found"                  # 用户不存在
check "$OUT" "already logged in"          # 已登录再 login
check "$OUT" "bad_args"                   # 少参数
check "$OUT" "welcome, alice"             # 登出后 login 成功

echo "OK L2 (账号流程)"

# ------------ L3：登录后 dict ------------
# 登录后跑查询、历史、收藏、取消收藏、查看收藏
OUT=$(run_cli \
    '.reg carol 123' \
    '.query apple' \
    '.query zzzzz' \
    '.query' \
    '.history' \
    '.star apple' \
    '.star apple' \
    '.pad' \
    '.unstar apple' \
    '.unstar apple' \
    '.pad' \
    '.quit')

check "$OUT" "welcome, carol"             # reg
check "$OUT" "apple"                      # query 命中（词头）
check "$OUT" "not_found"                  # query 不存在
check "$OUT" "bad_args"                   # query 无参
check "$OUT" "apple"                      # history 含 apple
check "$OUT" "starred"                    # star 重复
check "$OUT" "unstarred"                  # unstar 未收藏

echo "OK L3 (登录后 dict)"

echo "ALL OK it_cli_smoke"