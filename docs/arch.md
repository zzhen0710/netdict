# netdict 架构说明

## 1. 概述

netdict 是一个**基于 TCP 的网络英语字典**：客户端发送查询请求，服务端从 SQLite 词库返回释义。

- **语言**：C++17
- **并发模型**：`epoll` + 线程池（Reactor + worker）
- **数据**：SQLite（词典库 `dict.db` + 用户库 `usr.db`）
- **协议**：行协议（详见 `docs/proto.md`）

---

## 2. 分层

```
┌─────────────────────────────────────────────┐
│  cli（客户端）                                │
│    LineEditor / EditorHistory                │
│    poll 同时监听 stdin + socket               │
└────────────────┬────────────────────────────┘
                 │ TCP（行协议）
┌────────────────▼────────────────────────────┐
│  ser（服务端）                                │
│    epoll 主循环 + 线程池 + 管理终端            │
│    handleClient / handleSystem               │
└───────┬──────────────────────┬──────────────┘
        │                      │
┌───────▼──────┐      ┌────────▼───────┐
│  db（仓储）   │      │  common（公共） │
│  DictRepo    │      │  proto / net   │
│  UsrRepo     │      │  logger / utils│
└───────┬──────┘      │  guard / types │
        │             └────────────────┘
┌───────▼──────┐
│ SQLite       │
│ dict.db      │
│ usr.db       │
└──────────────┘
```

### 依赖方向

```
cli ──▶ common
ser ──▶ common + db
db  ──▶ common
```

**单向**，**无循环依赖**。`common` 是最底层（只依赖标准库 + SQLite）。

---

## 3. 模块职责

| 模块 | 职责 |
|------|------|
| `cli` | 客户端：连接、行编辑、发送请求、渲染响应 |
| `ser` | 服务端：监听、epoll 事件循环、线程池、命令分发、管理终端 |
| `db/dict_repo` | 词典表访问（查询 / 管理） |
| `db/usr_repo` | 用户 / 历史 / 收藏表访问 |
| `common/proto` | 协议编解码（命令 / 响应） |
| `common/net` | 网络 IO 工具（`sendAll` / `recvLine`） |
| `common/logger` | 日志（分级、输出文件） |
| `common/line_editor` | linenoise 封装（编辑会话 + 历史） |
| `common/guard` | RAII（`FdGuard` / `DbGuard` / `StmtGuard`） |
| `common/types` | 跨模块类型（`Meaning` / `status::*`） |
| `common/utils` | 工具（时间格式化、输出、`overloaded`） |

---

## 4. 服务端架构

### 4.1 总览

```
主线程：
  epoll_wait
    ├─ listen_fd_  → handleAccept（循环 accept 到 EAGAIN）
    ├─ STDIN_FILENO → handleSystem（管理终端，仅 tty）
    └─ conn_fd     → handleConn（摘 epoll + 派发）

线程池（4 worker）：
  handleClient(cfd)   ← 阻塞收发，直到连接关闭
```

### 4.2 epoll 模型

**主线程**用 `epoll` 监听：

- **`listen_fd_`**：新连接
- **`STDIN_FILENO`**：管理命令（**仅当 stdin 是 tty**）
- **所有未派发的 `conn_fd`**：连接就绪

**连接就绪后**（`handleConn`）：

1. **从 epoll 摘除**（`EPOLL_CTL_DEL`）——避免重复派发
2. **标记 `in_flight`**——防同一 fd 被多次入队
3. **`thread_pool_.addTask(handleClient)`**——交给 worker

**worker 阻塞收发**，直到连接关闭，**自己 `close` + `conns_.erase`**。

**关键设计**：

- **连接 fd 保持阻塞**——epoll 只负责"首次可读"通知；派发后 fd 独占
- **摘除 + 标记 + addTask 同一临界区**——防竞态
- **同一 fd 不会同时被两个线程处理**——`in_flight` + `DEL`

### 4.3 连接状态

```cpp
struct Conn {
    bool        in_flight = false;  // 是否已派发给 worker
    std::string usr;                // 登录用户名（空 = 未登录）
};

std::unordered_map<int, Conn> conns_;   // cfd → Conn
std::mutex                    conns_mtx_;
```

**会话（登录态）并入 `Conn.usr`**——不单独维护 `sessions_`。

**在线状态在内存**（不入库）：重启后"在线"天然正确；多设备登录自然支持。

### 4.4 空闲超时

**每个 `cfd` 设 `SO_RCVTIMEO`**（当前 5s，测试用）。worker 的 `recv` 超时 → `recvLine` 返回 `Timeout` → 踢连接。

**服务端踢前**先发 `err idle timeout, closing`，客户端收到 → 等 Enter → 退出。

### 4.5 停止流程

```
信号（SIGINT/SIGTERM）
  → 信号 handler：requestStop() 只置 running_ = false（async-signal-safe）
  → epoll_wait 返回 EINTR → 主循环退出
  → doStop()：
      1. 遍历 conns_，发 err server shutdown + shutdown(fd)
      2. thread_pool_.stop()
  → Server 析构：FdGuard / ThreadPool / optional<LineEditor> 自动清理
```

**`stop` 拆两段**（`requestStop` / `doStop`）：**信号 handler 只做最小操作**（`atomic store`），**清理在主循环退出后**（可加锁、可系统调用）。

### 4.6 管理终端

**stdin 进 epoll**（仅当 tty）。`handleSystem`：

1. `LineEditor::feed()` 读一行
2. 要求 `.` 开头，去点
3. `decodeSys` 解析
4. `visit` 分发到 `handleSysDict` / `handleSysCtrl`
5. 输出到 **stdout**（`utils::printLine`）

**和 `handleClient` 对称**（不同：输入 stdin / 输出 stdout / 无会话）。

---

## 5. 客户端架构

### 5.1 主循环

```
tty 模式：
  LineEditor（进 raw mode）
  poll 监听 stdin + socket
    ├─ stdin 可读  → feed() → 拿到行 → handleCmd
    └─ socket 可读 → MSG_PEEK 探测
                       ├─ 0 = 断开 → 打印 + 退出
                       └─ >0 = 有数据 → handleResp

非 tty 模式（管道 / 重定向）：
  runBatch：逐行 getline，不 poll
```

### 5.2 响应渲染

`handleResp`：

- `ok <n>` + n 行 → 读全
- **判首行** `--- ` 开头 → **分组渲染**（词头 + 缩进编号释义）
- 否则 → **纯文本原样输出**（help）

### 5.3 被动退出

服务端主动推 `err`（终态）→ 客户端显示 → **等用户按 Enter** → 退出（不突兀）。

---

## 6. 数据层

### 6.1 两个库

| 库 | 表 | 说明 |
|----|----|------|
| `dict.db` | `dict(word, pos, mean)` | 词典（一词多义多行） |
| `usr.db` | `usr(name, pwd)` | 用户 |
| | `history(name, word, pos, mean, time, batch)` | 历史（batch 分组） |
| | `star(name, word, pos, mean, time)` | 收藏 |

### 6.2 关键设计

- **`dict` 无主键**：同 `word` 多行（多义）
- **`star` 主键 `(name, word, pos, mean)`**：同词多义可存，重复收藏同释义才冲突
- **`history` 有 `batch`**：一次查询 = 一个 batch，用于分组
- **`star` 冗余 `mean` / `pos`**：收藏是"快照"，不跨库 JOIN

### 6.3 事务

- `initFromFile`：整批导入包事务
- `star` / `addHistory`：一个词的所有释义**原子插入**（事务）

### 6.4 RAII

`DbGuard`（连接）/ `StmtGuard`（预编译语句）——析构自动 `close` / `finalize`，异常安全。

---

## 7. 线程模型

| 线程 | 职责 |
|------|------|
| **主线程** | epoll 事件循环；accept；管理终端 |
| **worker（N）** | `handleClient`：一个连接阻塞收发 |

**共享数据**：`conns_`（`conns_mtx_` 保护）、`ThreadPool` 任务队列（内部锁）。

**SQLite**：默认 `SERIALIZED` 模式（多线程安全，慢）。

**日志**：`fprintf(FILE*)`——**对同一 `FILE*` 线程安全**（`_IOLBF` 行缓冲）。

---

## 8. 日志

- **分级**：`Debug / Info / Warn / Err / Off`
- **输出**：文件（`logs/netdict_server.log` / `logs/netdict_client.log`）——**避免和终端 prompt 混**
- **行缓冲**（`_IOLBF`）：每行及时落盘
- **热路径**：`LOG_*` 宏内部判等级；"为日志取数"用 `enabled` 包裹（**当前数据多现成，`enabled` 未用**）

---

## 9. 错误处理

| 层 | 手段 |
|----|------|
| **协议** | `ok` / `<stat> [reason]` |
| **handler** | 状态枚举（`status::UsrOp` / `Query` / `Admin`） |
| **`handleClient` / `handleSystem`** | `try/catch` 兜底——单请求 / 单命令异常**不崩进程** |
| **RAII** | `FdGuard` / `DbGuard` / `StmtGuard` / `LineEditor` / `ThreadPool` |
| **信号** | `requestStop` 只置 `atomic`（async-signal-safe） |

---

## 10. 测试

| 层 | 内容 |
|----|------|
| **单元** | `test_guard` / `test_net` / `test_proto` / `test_dict_repo` / `test_usr_repo` / `test_thread_pool` / `test_logger` / `test_utils` / `test_line_editor` / `test_handle_sys` |
| **集成** | `it_cli_smoke`（CLI 端到端）/ `it_concurrent`（并发连接） |

---

## 11. 目录

```
netdict/
├── CMakeLists.txt
├── README.md
├── data/                # dict.txt（TSV 词库）、*.db（运行时生成）
├── docs/                # arch / proto / deploy / adr
├── include/             # 对外接口，与 src/ 对应
│   ├── cli/
│   ├── common/          # net / proto / logger / utils / types / guard / line_editor
│   ├── db/
│   └── ser/
├── src/                 # 实现
├── tests/
│   ├── unit/
│   └── integration/
├── third_party/         # linenoise
└── logs/                # 运行时日志（不进 Git）
```

---

## 12. 关键决策（详见 `docs/adr/`）

- 在线状态放内存（不入库）
- `.` 只在输入层，协议不带点
- 响应三分（`ok` / `<stat> reason`）+ 分组
- `star` 冗余 `mean`（快照，不跨库 JOIN）
- epoll + 线程池（连接独占 worker）
- 空闲超时（`SO_RCVTIMEO`）
- `stop` 拆 `requestStop` / `doStop`（信号安全）
- `LineEditor` tty 适配（非 tty 走批量）