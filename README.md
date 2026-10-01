# netdict

基于 TCP 的网络英语字典：客户端发送查询请求，服务端从 SQLite 词库返回释义。

## 特性

- **C++17**，`epoll` + 线程池并发
- **行协议**（文本，`nc` 可测）
- **SQLite** 双库（词典 + 用户）
- **客户端行编辑**（`linenoise`，`↑/↓` 历史）
- **服务端本地管理终端**（stdin）
- **连接空闲超时**、**优雅关停**、**日志分级**

## 依赖

- `g++`（支持 C++17）
- `cmake`（≥ 3.12）
- `sqlite3` 开发库
- `pthread`

**Ubuntu / Debian**：

```bash
sudo apt install build-essential cmake libsqlite3-dev
```

**CentOS / RHEL**：

```bash
sudo yum install gcc-c++ cmake sqlite-devel
```

## 编译

```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

生成两个可执行文件（项目根）：

- `netdict_server`
- `netdict_client`

## 运行

**服务器**：

```bash
./netdict_server [ip] [port]
# 默认 0.0.0.0:13140
```

**客户端**：

```bash
./netdict_client [ip] [port]
# 默认 0.0.0.0:13140（本机测试用 127.0.0.1）
```

## 使用

### 客户端

输入以 `.` 开头（`.` 是输入友好层，协议不带点）。

```
netdict> .reg alice 12345
welcome, alice
netdict> .query apple
apple
  1. n. 苹果, 家伙
  2. [医] 苹果
netdict> .star apple
netdict> .pad
apple  [2026-09-30 17:10:05]
  1. n. 苹果, 家伙
  2. [医] 苹果
netdict> .quit
goodbye
```

**命令一览**：

```
[账号]
  .reg   <name> <pwd>          注册
  .login <name> <pwd>          登录
  .logout                      登出

[查词]
  .query   <word>              查词
  .history [num]               历史（默认 10 次）

[收藏]
  .star    <word>              收藏
  .unstar  <word>              取消收藏
  .pad     [num]               收藏列表（默认 10 词）

[其他]
  .help                        指令集
  .quit / .exit                退出
```

**行编辑**：`←/→` 移动、`Home/End`、`Backspace/Delete`、`Ctrl+W`（删词）、`Ctrl+U`（清行）、`↑/↓`（历史）。

### 管理终端

服务器**本地**（stdin）交互终端——**仅当 stdin 是 tty**。**同一个进程**，无需额外连接。

```
netdict admin console ready (type .help)
netdict> .num
12345 entries
netdict> .view apple
apple
  1. n. 苹果, 家伙
  2. [医] 苹果
netdict> .shutdown
server shutting down...
```

**命令一览**：

```
[字典查询]
  .list [name*]                列词（* 通配）
  .view <name>                 查看
  .num                         词条数

[字典编辑]
  .add <word> <pos> <mean>     加词
  .del <name>                  删词
  .update <word> <pos> <mean>  改词
  .reload                      重载 data/dict.txt

[用户]
  .stat    <usrname>           用户信息
  .history <usrname> [num]     用户历史
  .pad     <usrname> [num]     用户收藏

[服务器]
  .log <level>                 日志等级（debug/info/warn/err/off）
  .shutdown                    关服务器

[其他]
  .help                        指令集
```

## 数据

### 词库

**`data/dict.txt`**：TSV 三段格式，每行一条释义。

```
word<TAB>pos<TAB>mean
```

一词多义 = 多行。

```
apple	n.	苹果, 家伙
apple	[医]	苹果
like	v.	喜欢
like	prep.	像
```

- **首次启动**：`data/dict.db` 不存在时，自动从 `dict.txt` 导入。
- **重载**：管理终端 `.reload`（清空 + 重新导入）。

词库源：**ECDICT**（https://github.com/skywind3000/ECDICT）。
转换脚本：`tools/ecdict2tsv.py`（过滤人名/地名/网络 + 按考试标签筛选 + 提取段首词性）。

### 用户库

`data/usr.db`（首次启动自动建表）：用户 / 历史 / 收藏。

## 目录

```
netdict/
├── CMakeLists.txt
├── README.md
├── data/                # dict.txt（词库）；*.db 运行时生成
├── docs/                # 文档（arch / proto / deploy / adr）
├── include/             # 对外接口
│   ├── cli/
│   ├── common/          # net / proto / logger / utils / types / guard / line_editor
│   ├── db/
│   └── ser/
├── src/                 # 实现
├── tests/
│   ├── unit/            # 单元测试
│   └── integration/     # 集成测试
├── third_party/         # linenoise
├── tools/               # 辅助脚本（ecdict2tsv.py）
└── logs/                # 运行时日志（不进 Git）
```

## 文档

- [架构说明](docs/arch.md)
- [协议说明](docs/proto.md)
- [部署说明](docs/deploy.md)
- [设计决策（ADR）](docs/adr.md)

## 开发历程

6 天从零（C++ 基础 → 完整项目）。按天记：

| 天 | 内容 |
|----|------|
| **Day 1**（09-25） | 项目骨架；`Guard` 三件套（`FdGuard`/`DbGuard`/`StmtGuard`）；`types`（`status::*` + `Meaning`）；单元测试引入 |
| **Day 2**（09-26） | `proto` 协议 + 测试；`DictRepo`（建表/导入/查询）；`UsrRepo`（注册/登录/历史/收藏）；CMake + C++17 |
| **Day 3**（09-27） | `ThreadPool`；`logger`；`utils`/`net`；**Server 基础版**（accept + 收一行 + 回定长串）；`doQuery` 多行响应；用户命令全实现；客户端骨架 |
| **Day 4**（09-28） | 客户端完整；`linenoise` 行编辑 + 历史；信号优雅停止；会话加锁（多客户端）；`logger::enabled` 热路径优化；测试重组 `unit/` + `integration/` |
| **Day 5**（09-29） | **epoll 升级** + 配套重构；`LineEditor`/`EditorHistory` 封装；`recvLine` 枚举 + 连接超时；并发连接验证脚本；补单元测试 |
| **Day 6**（09-30） | 词条加 `pos` + `dict.txt` 改 TSV；分组响应 + 客户端渲染；`history` 的 `batch`；管理终端 `handle_sys_*`；日志走文件；`LineEditor` tty 适配；文档（README / arch / proto / deploy / adr） |

> 词库转换脚本（`tools/ecdict2tsv.py`）与最终 `data/dict.txt`（约 5.8 万词）
> 为 Day 6 完成、次日提交。

## 测试

```bash
cd tests
./run_all.sh              # 单元 + 集成
./run_all.sh unit         # 只单元
./run_all.sh integration  # 只集成
```

**依赖**：`python3`（集成测试用 socket 客户端）。

## 架构

```
cli ──▶ common
ser ──▶ common + db
db  ──▶ common
```

- **cli**：客户端（连接、行编辑、渲染）
- **ser**：服务端（`epoll` 主循环 + 线程池 + 管理终端）
- **db**：数据访问（`DictRepo` / `UsrRepo`）
- **common**：公共库（协议 / 网络 / 日志 / 工具 / RAII）

详见 [架构说明](docs/arch.md)。

## 许可

第三方库 `linenoise` 为 BSD-2-Clause（见 `third_party/README.md`）。