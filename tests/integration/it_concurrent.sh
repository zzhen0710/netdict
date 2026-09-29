#!/bin/bash
# ============================================================
#  it_concurrent.sh — 并发连接验证
# ============================================================
#  目的：验证服务器能同时服务多个客户端，观察"线程池大小 = 并发处理上限"。
#        当前线程池 4 线程 → 同时最多 4 个连接被处理。
#
#  预期结果：8/8。
#    原理：客户端"收到响应立即关闭"，worker 处理完一个连接马上轮转到下一个。
#    4 个 worker 轮流把 8 个连接处理完，全部成功。
#
#  反例（为什么不用"读超时"退出）：
#    若客户端靠"读超时"退出，则客户端存活时间 = 读超时；
#    worker 被占时间也 ≈ 读超时。前 4 个占满 worker 到超时，
#    后 4 个也几乎同时超时 → 后 4 个大概率收不到响应。
#    读超时越长（如 2s）这个问题越严重 → 反而更不稳。
#
#  依赖：
#    - make 生成 ./netdict_server
#    - python3（socket 做客户端；nc 收到响应不主动退会卡）
#    - timeout（coreutils 自带，给 python 客户端兜底）
#
#  流程：
#    1. 清 data/usr.db、旧日志
#    2. 生成 /tmp/netdict_py_client.py
#    3. 后台起 server，等日志 "listen on"
#       注：—— 刚启动等待 4~6 秒是正常的，原因：
#            a) server 构造里要建库、建表、建索引（SQLite 首次写盘）；
#            b) 初始化 epoll、线程池、监听 socket；
#            c) 日志经 stderr 重定向到文件，有缓冲/刷新延迟；
#            d) 脚本用轮询（每 0.1s 查日志），有检查间隔。
#          —— 与“网络”基本无关：本机 loopback，连接本身不慢。
#    4. 单连接注册用户 stress
#    5. 并发 N 个连接
#    6. 统计
#    7. 显式关 server（SIGTERM → 等 3s → SIGKILL）
#
#  端口：13998（避开默认 13140 / 集成测试 13999）
# ============================================================

set -e                          # 任一命令失败立即退出
cd "$(dirname "$0")/../.."      # 切到项目根
PORT=13998
N=${1:-8}                       # 并发数，默认 8

# ---------- 前置检查 ----------
[ -x ./netdict_server ] || { echo "缺少 server（先 make）"; exit 1; }
command -v python3 >/dev/null || { echo "缺少 python3（安装见文件头）"; exit 1; }

rm -f data/usr.db               # 删旧用户库，保证干净
rm -f /tmp/netdict_stress_ser.log

# ---------- 生成 python 客户端 ----------
# 写成文件：N 个后台任务共用一份，避免 heredoc 并发展开的不确定性。
PY_CLIENT=/tmp/netdict_py_client.py
cat > "$PY_CLIENT" <<'PY'
import socket, sys

port  = int(sys.argv[1])
lines = sys.argv[2:]                    # 要发的每一行（不含 '\n'）

s = socket.socket()
s.settimeout(5)                         # 连接 + 读的统一兜底超时（正常不会用到）
try:
    s.connect(("127.0.0.1", port))

    # 拼 "line1\nline2\n..." 一次发出；服务端按 '\n' 切分
    payload = "".join(l + "\n" for l in lines)
    s.sendall(payload.encode())

    # 读到 "apple"（query 的释义）立即停，不再等下一批数据。
    # 关键：收到就关，worker 马上能轮转到下一个连接。
    data = b""
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:               # 对端关闭
                break
            data += chunk
            if b"apple" in data:        # 目标响应已到，收工
                break
    except socket.timeout:
        pass                            # 兜底：超时也算收工

    sys.stdout.buffer.write(data)
except Exception as e:
    print("connect failed:", e)
finally:
    s.close()
PY

# ---------- 起服务器 ----------
./netdict_server 0.0.0.0 "$PORT" > /tmp/netdict_stress_ser.log 2>&1 &
SER_PID=$!
trap 'kill $SER_PID 2>/dev/null || true; rm -f /tmp/netdict_stress_*.log "$PY_CLIENT"' EXIT

# 等 "listen on"（最多 2 秒）。用 if 包 grep，防 set -e 首次未命中误退。
for _ in $(seq 1 20); do
    if grep -q "listen on" /tmp/netdict_stress_ser.log; then
        break
    fi
    sleep 0.1
done
if ! grep -q "listen on" /tmp/netdict_stress_ser.log; then
    echo "服务器未启动"
    cat /tmp/netdict_stress_ser.log
    exit 1
fi

# ---------- 辅助：一个连接，发给定行，输出到 $1 ----------
# timeout -k 1 5：5 秒 SIGTERM；再 1 秒不死 SIGKILL，防 python 卡住。
run_lines() {
    local out="$1"; shift
    timeout -k 1 5 python3 "$PY_CLIENT" "$PORT" "$@" > "$out" 2>&1 || true
}

# ---------- 注册测试用户 ----------
run_lines /tmp/netdict_stress_reg.log "reg stress 123"

# ---------- 并发 N 个连接 ----------
echo "并发 $N 个客户端..."

client_pids=()                          # 收集客户端 PID
for i in $(seq 1 "$N"); do
    # 后台起一个客户端：login + query，输出各自日志
    run_lines "/tmp/netdict_stress_$i.log" \
              "login stress 123" "query apple" &
    client_pids+=($!)
done

# 【关键】只 wait 客户端 PID，不等 server。
# 无参数 wait 会等所有子进程（含跑主循环的 server），永远卡住。
for pid in "${client_pids[@]}"; do
    wait "$pid" 2>/dev/null || true
done

# ---------- 统计 ----------
ok=0
fail=0
# 逐个检查输出有没有 "apple"，有则 OK，否则 FAIL 并打印
for i in $(seq 1 "$N"); do
    if grep -q "apple" "/tmp/netdict_stress_$i.log"; then
        ok=$((ok + 1))
    else
        fail=$((fail + 1))
        echo "--- client $i 无响应 ---"
        cat "/tmp/netdict_stress_$i.log"
    fi
done

# 汇总
echo "================================"
echo "并发 $N：OK=$ok  FAIL=$fail"
echo "================================"

# ---------- 显式关服务器 ----------
# SIGTERM 可能不够（server 卡在 ThreadPool join 上不退），端口被占下轮起不来。
# 策略：SIGTERM → 最多等 3 秒 → 强杀。
kill "$SER_PID" 2>/dev/null || true

for _ in $(seq 1 30); do
    kill -0 "$SER_PID" 2>/dev/null || break   # 进程没了就退出
    sleep 0.1
done

kill -9 "$SER_PID" 2>/dev/null || true   # 还不死，强杀
wait "$SER_PID" 2>/dev/null || true      # 回收僵尸

sleep 0.3                                # 等内核释放端口