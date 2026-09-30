# netdict 架构决策记录（ADR）

> 记录重要设计决策的"背景 / 决策 / 结果"。
> 状态：已接受 / 提议 / 已废弃 / 已替代。

## 索引

| 编号 | 主题 | 状态 |
|------|------|------|
| [0001](#adr-0001) | 在线状态放内存，不入库 | 已接受 |
| [0002](#adr-0002) | quit / logout / shutdown 分三义 | 已接受 |
| [0003](#adr-0003) | `.` 只在输入层，协议不带点 | 已接受 |
| [0004](#adr-0004) | 响应三分 + reason | 已接受 |
| [0005](#adr-0005) | star 表冗余 mean（快照） | 已接受 |
| [0006](#adr-0006) | 单线程 accept + 线程池 | 已被 0011 替代 |
| [0007](#adr-0007) | cli 用 linenoise | 已接受 |
| [0008](#adr-0008) | 日志分级 | 已接受 |
| [0009](#adr-0009) | TCP 上不做超时重传 | 已接受 |
| [0010](#adr-0010) | 错误 reason 用字符串，不扩枚举 | 已接受 |
| [0011](#adr-0011) | epoll + 线程池（替代 0006） | 已接受 |
| [0012](#adr-0012) | 连接空闲超时（SO_RCVTIMEO） | 已接受 |
| [0013](#adr-0013) | stop 拆 requestStop / doStop | 已接受 |
| [0014](#adr-0014) | 连接状态合并为 conns_ | 已接受 |
| [0015](#adr-0015) | 分组响应协议（`--- word`） | 已接受 |
| [0016](#adr-0016) | 词条加 pos，dict.txt 用 TSV 三段 | 已接受 |
| [0017](#adr-0017) | 收藏 / 历史按"词"原子写 | 已接受 |
| [0018](#adr-0018) | history 的 batch 列 | 已接受 |
| [0019](#adr-0019) | LineEditor tty 适配 + 非 tty batch | 已接受 |
| [0020](#adr-0020) | 日志走文件，不入 stderr | 已接受 |
| [0021](#adr-0021) | 管理终端同进程，stdin 进 epoll | 已接受 |

---

## ADR-0001

**主题**：在线状态放内存，不入库
**状态**：已接受（2026-09-28）

**背景**：服务器要知道"谁在线"。可选：持久化（`usr.stage`）或内存。

**决策**：**在线状态放内存**（当前实现：`conns_[cfd].usr`）。

**结果**：

- **好**：重启后"在线"天然正确，无需"启动清 stage"；多设备登录自然支持。
- **代价**：多服务器要"共享会话"（Redis / DB）；进程崩则全掉线（**符合直觉**）。

---

## ADR-0002

**主题**：quit / logout / shutdown 分三义
**状态**：已接受

**背景**：客户端要"登出 / 断开"，服务器要"关停"。

**决策**：

- `UsrCmd::Ctrl::Logout`（`.logout`，清会话）
- `UsrCmd::Ctrl::Quit`（`.quit` / `.exit`，断开连接）
- `SysCmd::Ctrl::Shutdown`（`.shutdown`，关服务器）

**结果**：语义明确；`decodeUsr` / `decodeSys` 各自映射别名。

---

## ADR-0003

**主题**：`.` 只在输入层，协议不带点
**状态**：已接受

**背景**：用户喜欢 `.query apple`；协议要干净。

**决策**：**客户端 / 管理终端去点后发送**；`proto` **不认带点命令**。

**结果**：

- 协议仿 Linux / IRC / FTP；`.` 是"输入友好"，**不是"协议"**。
- `nc` 手动测时**发不带点**。
- **客户端**（`sendReq`）和**管理终端**（`handleSystem`）**都去点**。

---

## ADR-0004

**主题**：响应三分 + reason
**状态**：已接受（扩展见 0015）

**背景**：只有 `ok` / `err` 太粗；所有错都加枚举会爆。

**决策**：

- `ok [data]` / `<stat> [reason]`
- **业务明确的状态**用枚举（`not_found` / `exists` / `wrong_pwd` / `starred` / `unstarred` / `bad_args` / `forbidden`）
- **通用失败**用 `err` + `reason`（`already logged in` / `bad args`）

**结果**：

- `respData` 管 `ok`，`respReason` 取 `stat` 之后。
- 仿 HTTP / gRPC / errno。

---

## ADR-0005

**主题**：star 表冗余 mean（快照）
**状态**：已接受（扩展见 0016）

**背景**：`pad` 要显示"词 + 义"；`star` 在 `usr.db`，`dict` 在 `dict.db`。

**决策**：**`star` 表加 `pos` / `mean` 列**，收藏时**写快照**。

**结果**：

- **`UsrRepo` 不跨库**，`pad` 一次查询。
- **代价**：`dict` 改义时 `star.mean` **陈旧**（可接受——"收藏"即快照）。
- **备选**：逐词 `dict_.query`（N 次）、`ATTACH JOIN`（**破分层**）。

---

## ADR-0006

**主题**：单线程 accept + 线程池
**状态**：**已被 [ADR-0011](#adr-0011) 替代**

**背景**：初期单线程串行；要并发。

**决策**（当时）：`accept` 后 `thread_pool_.addTask` 处理连接；`sessions_` 加 mutex。

**结果**：多客户端并发；**但主循环 `accept` 阻塞，无法同时监听其他事件**——**升级为 epoll**。

---

## ADR-0007

**主题**：cli 用 linenoise
**状态**：已接受

**背景**：想支持行编辑 + `↑/↓` 历史。

**决策**：

- `third_party/linenoise.{c,h}`；`add_library(linenoise STATIC)`
- `project(netdict C CXX)`（**启用 C**，为 linenoise）
- 历史存 `~/.netdict_cli_history`（用户级）
- `kCliHistoryMaxLen = 100`（环形覆盖）

**结果**：cli 行编辑 + 历史；无 readline 依赖；`Save` **全量覆盖**非追加。

---

## ADR-0008

**主题**：日志分级
**状态**：已接受（扩展见 0020）

**背景**：日志要能**运行期开关**，但"为日志取数"（如 `usrGet` 加锁）不能白跑。

**决策**：

- `logger::g_level`（运行期，`setLevel` 头内 `inline`）
- **仅为日志的取数**用 `if (logger::enabled(...)) { 取数; LOG_*; }` 包裹
- `LOG_*` 宏**内部判级别**（数据现成时直接用）

**结果**：

- 热路径"关时零开销"。
- **当前 `enabled` 未使用**（数据多现成）——保留备用。

---

## ADR-0009

**主题**：TCP 上不做超时重传
**状态**：已接受

**背景**：曾做 TFTP（UDP）需自做超时 / 重传。

**决策**：**TCP 项目不做重传**；"客户端出错 / 断连"由 TCP（FIN / RST）表示；应用层只做业务语义。

**结果**：分层清晰，不重复造轮子。

---

## ADR-0010

**主题**：错误 reason 用字符串，不扩枚举
**状态**：已接受

**背景**：`already logged in` 这种"通用失败"要不要加枚举。

**决策**：**不加**。用 `err` + `reason`。**判据**：客户端需特殊处理才加枚举。

**结果**：枚举不爆；`proto::respReason` 取通用原因。

---

## ADR-0011

**主题**：epoll + 线程池（替代 0006）
**状态**：已接受（2026-09-29）

**背景**：0006 的"阻塞 accept"无法同时监听 stdin / 多连接。

**决策**：

- **主线程** `epoll_wait`，监听 `listen_fd_` + 未派发的连接 fd（+ stdin，见 0021）。
- **连接就绪**（`handleConn`）：**`EPOLL_CTL_DEL` 摘除** + 标记 `in_flight` + `addTask`（同一临界区）。
- **worker** 阻塞收发（`handleClient`），直到连接关闭，**自己 `close` + `conns_.erase`**。
- **连接 fd 保持阻塞**——epoll 只负责"首次可读"通知；派发后 fd 独占。
- `handleAccept` **循环 `accept` 到 `EAGAIN`**（`listen_fd_` 非阻塞）。

**结果**：

- **同一 fd 不会被两个线程处理**（`in_flight` + `DEL`）。
- **线程池大小 = 同时处理连接数上限**；短连接可轮转。
- 代价：每连接至少一次 `epoll_ctl DEL`（系统调用）。

---

## ADR-0012

**主题**：连接空闲超时（SO_RCVTIMEO）
**状态**：已接受

**背景**：客户端占着连接不发数据也不关 → worker 永久阻塞在 `recv`。

**决策**：

- **`cfd` 设 `SO_RCVTIMEO`**（当前 3600s）。
- worker 的 `recv` 超时 → `recvLine` 返回 `RecvLineResult::Timeout` → **踢连接**。
- **踢前先发** `err idle timeout, closing` 通知客户端。

**结果**：

- 空闲连接自动回收。
- **`SO_RCVTIMEO` 是本地超时**——服务端"等对端数据等够"即踢；客户端不知情，靠服务端通知。
- `recvLine` 返回值从 `bool` 扩为枚举（`Ok` / `Timeout` / `Closed` / `Error`）。

---

## ADR-0013

**主题**：stop 拆 requestStop / doStop
**状态**：已接受

**背景**：`stop()` 里加锁、遍历、`thread_pool_.stop()`——**非 async-signal-safe**；信号 handler 调会**死锁**。

**决策**：

- **`requestStop()`**：**只置 `running_ = false`**（`std::atomic<bool>` store）——**async-signal-safe**——**信号 handler 调**。
- **`doStop()`**：**遍历 `conns_` 通知 + `shutdown` + `thread_pool_.stop()`**——**主循环退出后**调。

**结果**：

- 信号 handler **只做最小操作**，不死锁。
- 时序：信号 → `epoll_wait` EINTR → 主循环退 → `doStop`。
- **`Cli::stop()` 不拆**——它只有 `atomic store`，本就 async-signal-safe。

---

## ADR-0014

**主题**：连接状态合并为 conns_
**状态**：已接受

**背景**：原 `sessions_`（`unordered_map<int, string>`）和 `in_flight_`（`unordered_set<int>`）**两个结构、两把锁**。

**决策**：**合并为 `conns_`（`unordered_map<int, Conn>`）**：

```cpp
struct Conn {
    bool        in_flight;  // 已派发给 worker
    std::string usr;        // 登录用户名（空 = 未登录）
};
```

**结果**：

- **一份状态、一把锁**（`conns_mtx_`）。
- **`key` 即 fd**——`Conn` 不存 `fd`（避免冗余）。
- `usrGet` / `usrSet` / `usrClear` 操作 `conns_[fd].usr`。
- **`usrClear` 只清 `usr`，不删条目**（连接还在）。

---

## ADR-0015

**主题**：分组响应协议（`--- word`）
**状态**：已接受

**背景**：`query` / `pad` / `history` 要"一个词 + 多条释义"，平铺行不清晰。

**决策**：

- **`ok <总行数>` + n 行**。
- **词头行**：`--- <word>[\t<time>]`。
- **释义行**：`<pos>\t<mean>`。
- **客户端渲染**：`word` + `  1. pos mean`。
- **纯文本多行**（如 `help`）：首行**非 `--- `**，客户端**原样打**。

**结果**：

- 一词多义 = 一词头 + 多释义行。
- 同词多次查询（history）= **多词头**（按 `batch` 区分，见 0018）。
- 客户端 **`--- ` 前缀判分组**。

---

## ADR-0016

**主题**：词条加 pos，dict.txt 用 TSV 三段
**状态**：已接受

**背景**：原 `dict(word, mean)`——**词性混在 `mean` 里**，无法独立筛选 / 显示。

**决策**：

- **`Meaning { pos, mean }`** / **`DictEntry { word, pos, mean }`**。
- **`dict` / `star` / `history` 表加 `pos` 列**。
- **`data/dict.txt` 用 TSV 三段**：`word<TAB>pos<TAB>mean`。
- 一词多义 = 多行。

**结果**：

- 词性独立（可筛选、可显示）。
- `initFromFile` 解析 TSV（切两个 `\t`）。
- 词库源：**ECDICT**（过滤 + 段首词性提取）——脚本 `tools/ecdict2tsv.py`。

---

## ADR-0017

**主题**：收藏 / 历史按"词"原子写
**状态**：已接受

**背景**：一词多义 → 多行。`.star like` 应收藏**所有释义**；`.query` 应记**所有释义**。

**决策**：

- **`star(name, word, vector<Meaning>, time)`**：**事务**插该 word 所有释义。
- **`addHistory(name, word, vector<Meaning>, time)`**：同。
- **`unstar(name, word)`**：删该 word 所有行。
- **`star` 主键 `(name, word, pos, mean)`**：同词多义可存，重复收藏同释义才冲突。
- **"已收藏"**：该 word 在表里有**任何一行**。

**结果**：

- "收藏一词 = 收藏所有释义"（原子）。
- 无"部分收藏"中间态。

---

## ADR-0018

**主题**：history 的 batch 列
**状态**：已接受

**背景**：同词多次查询，`(word, time)` 可能**同秒**，无法区分 → 分组错乱。

**决策**：

- **`history` 表加 `batch INTEGER`**：每次 `addHistory` 分配 `max(batch)+1`（该 `name` 下），**同次所有行共享**。
- **`getHistory`**：`group by (word, batch)`；`order by batch desc, rowid asc`。
- **`doHistory`**：判 **`(word, batch)`** 换词头。

**结果**：

- 同词多次查询 = **多个词头**（不同 `batch`）。
- 组间：最新查询在前；组内：插入顺序。

---

## ADR-0019

**主题**：LineEditor tty 适配 + 非 tty batch
**状态**：已接受

**背景**：集成测试用管道驱动客户端（`printf ... | netdict_client`）；`linenoise` 假设 stdin 是 tty → `EditStart` 失败 / `poll` 卡死。

**决策**：

- **`LineEditor` 判 `isatty`**：
  - tty → 走 linenoise（行编辑）
  - 非 tty → 走 `std::getline`
- **`Cli::run` 判 `isatty`**：
  - tty → `poll` + `LineEditor`
  - 非 tty → **`runBatch`**（逐行 `getline`，**不 `poll`**）
- **`Server` 判 `isatty`**：非 tty **不建管理终端**（见 0021）。

**结果**：

- 管道 / 脚本驱动客户端可用。
- **`poll` 探内核，`std::cin` 缓冲对它是黑盒**——**两者不能混用**（非 tty 不 `poll`）。

---

## ADR-0020

**主题**：日志走文件，不入 stderr
**状态**：已接受

**背景**：`LOG_*` 走 `stderr` 与终端 prompt（`stdout`）**混行**。

**决策**：

- **`logger::setFile(path)`**：日志写文件（`logs/netdict_server.log` / `netdict_client.log`）。
- **`_IOLBF`（行缓冲）**：`setvbuf`，每行及时落盘。
- **`Level::Off`**：全关。
- **ser / cli main** 启动时 `setFile`；失败退回 `stderr` + `LOG_WARN`。

**结果**：

- 终端只剩 prompt，日志干净。
- 追加写，不清空。
- `\n` 用 `\n`（文件，非 `\r\n`）。

---

## ADR-0021

**主题**：管理终端同进程，stdin 进 epoll
**状态**：已接受

**背景**：服务器需要本地管理（查 / 改词库、看用户、关停）。**独立进程** or **同进程**。

**决策**：

- **同进程**：stdin 进 `epoll`（**仅当 stdin 是 tty**）。
- **`handleSystem`**：`LineEditor::feed()` 读一行 → 要求 `.` 开头（去点）→ `decodeSys` → `visit` → `handleSysDict` / `handleSysCtrl`。
- **输出 stdout**（`utils::printLine`），**无 cfd**、**无会话**。
- **`handleSystem` 和 `handleClient` 对称**（不同：输入 / 输出 / 会话）。

**结果**：

- 无需额外进程 / 端口 / 客户端。
- 非 tty（后台 / 重定向）**自动禁用**。
- `handleSystem` 加 `try/catch`（主线程异常 → 提示，**不崩服务器**）。
- `cmd` 归位 `handle_sys_dict` / `handle_sys_ctrl`（删 `common/cmd`）。

---

## 非目标 / 未来（本项目不做）

以下**不在本项目范围**，仅记录"若将来扩展"的可能方向：

- 多服务器共享会话（Redis）——若水平扩展
- 数据库 schema 迁移（ALTER TABLE）——若线上部署
- handleClient 状态机化（非阻塞 IO）——若极端高并发
- TLS——若跨公网