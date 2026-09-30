# netdict 部署说明

## 1. 环境要求

### 1.1 操作系统

- **Linux**（`epoll` 依赖）；推荐内核 ≥ 3.0
- 已在 **CentOS 7 / RHEL 7** 等测试

### 1.2 编译期依赖

| 依赖 | 版本 | 说明 |
|------|------|------|
| `g++` / `clang++` | 支持 **C++17** | GCC ≥ 7 或 Clang ≥ 5 |
| `cmake` | ≥ 3.12 | `CMAKE_CXX_STANDARD` 支持 |
| `libsqlite3-dev` | ≥ 3.0 | SQLite 头文件 + 库 |
| `pthread` | — | 线程库 |

**安装**：

```bash
# Ubuntu / Debian
sudo apt install build-essential cmake libsqlite3-dev

# CentOS / RHEL / Fedora
sudo yum install gcc-c++ cmake sqlite-devel
```

### 1.3 运行期依赖

- **`libsqlite3.so`**（编译期已链）
- **无其他**

### 1.4 可选工具

| 工具 | 用途 |
|------|------|
| `python3` | 集成测试（`tests/integration`） |
| `nc` | 手动测协议 |

---

## 2. 编译

```bash
git clone <repo-url> netdict
cd netdict

mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

**产物**（项目根目录）：

- `netdict_server`
- `netdict_client`

### 2.1 构建类型

```bash
# 默认（含 -Wall -Wextra）
cmake ..
make

# Release（优化）
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

### 2.2 清理重编

```bash
cd build
rm -rf *
cmake ..
make -j$(nproc)
```

---

## 3. 目录布局（运行）

**运行前**，项目根应有：

```
netdict/
├── netdict_server      # 可执行
├── netdict_client      # 可执行
├── data/
│   ├── dict.txt        # 词库（必需，首次导入）
│   ├── dict.db         # 运行时生成
│   └── usr.db          # 运行时生成
└── logs/               # 运行时生成（日志）
```

**启动时**：

- `data/dict.db` 不存在 → **自动建表** + **从 `data/dict.txt` 导入**
- `data/usr.db` 不存在 → **自动建表**
- `logs/` 不存在 → **自动 `mkdir`**

---

## 4. 运行

### 4.1 服务器

```bash
./netdict_server [ip] [port]
```

| 参数 | 默认 | 说明 |
|------|------|------|
| `ip` | `0.0.0.0` | 监听所有网卡；本机测试用 `127.0.0.1` |
| `port` | `13140` | 监听端口 |

**示例**：

```bash
# 前台
./netdict_server

# 指定
./netdict_server 127.0.0.1 8080

# 后台（日志走 logs/netdict_server.log）
nohup ./netdict_server > /dev/null 2>&1 &
```

**注意**：**后台运行时**，`stdin` **不是 tty**（`nohup` / `&`）→ **管理终端自动禁用**（正常）。

### 4.2 客户端

```bash
./netdict_client [ip] [port]
```

| 参数 | 默认 | 说明 |
|------|------|------|
| `ip` | `0.0.0.0` | 服务器 IP（本机测试用 `127.0.0.1`） |
| `port` | `13140` | 服务器端口 |

**示例**：

```bash
./netdict_client 127.0.0.1 13140
```

### 4.3 管理终端

**交互式运行服务器**（前台、tty）时，**自动启用**管理终端：

```
netdict admin console ready (type .help)
netdict> .num
12345 entries
```

**后台 / 重定向运行**（非 tty）时，**自动禁用**（日志会提示）。

---

## 5. 日志

| 日志 | 路径 |
|------|------|
| 服务器 | `logs/netdict_server.log` |
| 客户端 | `logs/netdict_client.log` |

- **行缓冲**（`_IOLBF`），每行及时落盘
- **分级**：`debug` / `info` / `warn` / `err` / `off`（默认 `info`）
- **运行期切换**：管理终端 `.log <level>`
- **追加写**，不清空；重启继续追加

**查看**：

```bash
tail -f logs/netdict_server.log
```

---

## 6. 停止

### 6.1 前台

**`Ctrl+C`（SIGINT）** 或 **`kill <pid>`（SIGTERM）**：

```
^C
[INFO] server loop exited
[INFO] server stopped
```

**流程**：`requestStop` → 主循环退出 → `doStop`（通知客户端 + 停线程池）→ 析构。

### 6.2 后台

```bash
kill <pid>          # SIGTERM（优雅）
kill -9 <pid>       # SIGKILL（强杀，不推荐）
```

### 6.3 管理终端

```
netdict> .shutdown
server shutting down...
```

**效果**同 SIGTERM。

### 6.4 客户端侧

服务器关停时，**客户端**会收到 `err server shutdown` → 显示 → **按 Enter 退出**。

---

## 7. 网络 / 防火墙

**默认端口 `13140`**。对外开放时：

```bash
# firewalld
sudo firewall-cmd --add-port=13140/tcp --permanent
sudo firewall-cmd --reload

# ufw
sudo ufw allow 13140/tcp
```

**仅本机测试**：绑 `127.0.0.1`。

---

## 8. 数据备份

**关键数据**：

- `data/dict.txt`（词库源，**进 Git**）
- `data/usr.db`（用户数据，**运行时生成**）

**备份**：

```bash
cp data/usr.db data/usr.db.bak.$(date +%F)
```

**词库更新**：

```bash
# 1. 更新 dict.txt
# 2. 管理终端 .reload（清空 + 重新导入）
```

---

## 9. 故障排查

| 现象 | 原因 / 处理 |
|------|-----------|
| `bind failed` | **端口被占**——`ss -tlnp \| grep 13140`，杀占用进程 |
| `socket failed` | 系统 fd 上限——`ulimit -n` |
| 客户端 `connect failed` | 服务器没起 / IP 端口错 / 防火墙 |
| 启动卡住（`SIGTTIN`） | **后台运行且 stdin 是 tty**——加 `< /dev/null` |
| `no such column` | 数据库 schema 旧——**删 `data/*.db` 重启**（开发期） |
| 日志空 | 缓冲 / 权限——检查 `logs/` 可写 |
| 词库没导入 | `data/dict.txt` 缺失 / 格式错（需 TSV 三段） |

**开发期"删库重建"**（schema 变更后）：

```bash
rm -f data/dict.db data/usr.db
./netdict_server
```

**正式**：**schema 变更要做迁移**（`ALTER TABLE`），**不能删库**。

---

## 10. 性能调优

| 项 | 位置 | 说明 |
|----|------|------|
| **线程池大小** | `ser.cpp` `thread_pool_(4)` | 按 CPU / 负载调 |
| **空闲超时** | `ser.cpp` `kRecvTimeoutSec` | 当前 **3600s（1 小时）** |
| **`epoll_wait` 批量** | `ser.cpp` `kMaxEvents` | 当前 1024 |
| **日志等级** | `.log warn`（生产） | 减少 I/O |
| **SQLite 索引** | `dict(word)` / `history(name)` | 已建 |