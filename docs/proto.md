# netdict 协议说明

## 1. 概述

netdict 是**基于 TCP 的行协议**（line-based protocol）：请求与响应均以 `\n` 结尾，UTF-8 编码。

- **传输层**：TCP
- **应用层**：文本行，`\n` 分隔
- **编码**：UTF-8（无 BOM）
- **默认端口**：`13140`

设计取向：**文本可读、`nc` 可测、仿 Linux/IRC/FTP 风格**。协议层**不带前导 `.`**（`.` 是客户端输入友好层，见 §7）。

---

## 2. 请求

客户端发送**一行**：

```
<cmd> [arg1] [arg2] ...
```

- `cmd`：命令名（小写，无点）
- `arg`：参数，空格分隔；参数内**不含空格**（如需空格，另行约定）
- 行尾：`\n`

**例**：

```
query apple
login alice 12345
star like
```

---

## 3. 响应

响应分**两类**。

### 3.1 单行响应

```
ok [data]
```

或

```
<stat> [reason]
```

- **成功**：首词 `ok`，其后为可选数据 `data`
- **失败**：首词为**状态词**（`not_found` / `exists` / `wrong_pwd` / `err` 等），其后为可选原因 `reason`

**例**：

```
ok
ok welcome, alice
not_found
err already logged in
err bad request
wrong_pwd
```

### 3.2 多行响应（结构化）

首行 `ok <n>`，`<n>` 为**后续数据行数**；其后跟 `n` 行数据。

```
ok <n>
<line 1>
<line 2>
...
<line n>
```

多行响应分**两种子类型**，由**首数据行**区分：

#### (a) 分组响应

**首数据行以 `--- ` 开头**，用于"一个词 + 多条释义"的结构化数据（`query` / `pad` / `history`）。

格式：

```
ok <总行数>
--- <word>[\t<time>]      ← 词头行（time 可选，pad/history 带）
<pos>\t<mean>             ← 释义行
<pos>\t<mean>
--- <word>[\t<time>]
...
```

- **词头行**：`--- ` 前缀 + `word`；`\t` 后可选 `time`（`pad` / `history` 用）
- **释义行**：`pos` + `\t` + `mean`（`pos` 可为空，即行首 `\t`）

**客户端渲染**（建议）：

```
word
  1. pos mean
  2. pos mean
```

同词多释义 = 一个词头 + 多行释义。
同词多次查询（`history`）= **多个词头**（按批次区分，见 §5.3）。

#### (b) 纯文本多行

**首数据行不以 `--- ` 开头**，用于帮助文本（`help`）等。

客户端**逐行原样输出**，不加编号。

---

## 4. 状态词

| 状态词 | 含义 | 场景 |
|--------|------|------|
| `ok` | 成功 | 所有命令 |
| `not_found` | 未找到 | 查词无果 / 用户不存在 / 词不存在 |
| `exists` | 已存在 | 注册重名 |
| `wrong_pwd` | 密码错 | 登录 |
| `starred` | 已收藏 | `.star` 重复 |
| `unstarred` | 未收藏 | `.unstar` 时本就未收藏 |
| `bad_args` | 参数错 | 参数数量 / 格式不符 |
| `forbidden` | 禁止 | 客户端发管理命令 |
| `err` | 其他错误 | 通用失败，附 `reason` |

**设计原则**：**业务上需客户端特殊处理的**用独立状态词；**通用失败**用 `err` + `reason`（字符串原因），**不无限扩枚举**。

---

## 5. 命令表

### 5.1 用户命令（UsrCmd）

客户端可发；需登录后使用查词 / 收藏类命令。

#### 5.1.1 控制（Ctrl）

| 命令 | 参数 | 响应 | 说明 |
|------|------|------|------|
| `reg` | `<name> <pwd>` | `ok welcome, <name>` / `exists` / `bad_args` | 注册（成功即登录） |
| `login` | `<name> <pwd>` | `ok welcome, <name>` / `not_found` / `wrong_pwd` | 登录 |
| `logout` | — | `ok` / `err not logged in` | 登出 |
| `help` | — | 多行（纯文本） | 指令集 |
| `quit` / `exit` | — | `ok` | 断开连接 |

#### 5.1.2 字典（Dict）

| 命令 | 参数 | 响应 | 说明 |
|------|------|------|------|
| `query` | `<word>` | 多行（分组） | 查词；记历史 |
| `history` | `[num]` | 多行（分组） | 自己的历史（默认 10 次） |
| `star` | `<word>` | `ok` / `starred` / `not_found` | 收藏（全部释义） |
| `unstar` | `<word>` | `ok` / `unstarred` | 取消收藏 |
| `pad` | `[num]` | 多行（分组） | 收藏列表（字母序，默认 10 词） |

**响应示例**（`.query apple`）：

```
ok 2
--- apple
n.	苹果, 家伙
[医]	苹果
```

**客户端渲染**：

```
apple
  1. n. 苹果, 家伙
  2. [医] 苹果
```

### 5.2 管理终端命令（SysCmd）

**仅本地管理终端**（服务器进程 stdin）；客户端发送将被拒（`forbidden`）。

#### 5.2.1 字典（Dict）

| 命令 | 参数 | 说明 |
|------|------|------|
| `list` | `[name*]` | 列词（`*` 通配，转 SQL `%`） |
| `view` | `<name>` | 查看一个词 |
| `add` | `<word> <pos> <mean>` | 加词 |
| `del` | `<name>` | 删词（该词所有释义） |
| `update` | `<word> <pos> <mean>` | 改词（全替换） |
| `reload` | — | 从 `data/dict.txt` 重载 |
| `num` | — | 词条总数 |

#### 5.2.2 控制（Ctrl）

| 命令 | 参数 | 说明 |
|------|------|------|
| `stat` | `<usrname>` | 用户信息（在线 / 历史数 / 收藏数） |
| `history` | `<usrname> [num]` | 指定用户历史 |
| `pad` | `<usrname> [num]` | 指定用户收藏 |
| `log` | `<level>` | 切日志等级（`debug`/`info`/`warn`/`err`/`off`） |
| `shutdown` | — | 关服务器 |
| `help` | — | 管理指令集 |

### 5.3 连接生命周期通知（服务端主动推）

服务端在以下情形**主动向客户端发送响应格式的通知**（非请求-响应配对）：

| 通知 | 状态词 | 客户端行为 |
|------|--------|-----------|
| 连接空闲超时 | `err idle timeout, closing` | 显示 → 等 Enter → 退出 |
| 服务器关停 | `err server shutdown` | 显示 → 等 Enter → 退出 |

**客户端处理**：收到 `err`（终态）→ 显示原因 → **等用户按 Enter** → 退出（被动退出）。

---

## 6. 示例会话

### 6.1 查词

```
→ query apple
← ok 2
← --- apple
← n.	苹果, 家伙
← [医]	苹果
```

### 6.2 登录 + 收藏 + 收藏列表

```
→ login alice 12345
← ok welcome, alice

→ star like
← ok

→ star like
← starred

→ pad
← ok 4
← --- like	2026-09-30 17:10:03
← v.	喜欢
← prep.	像
← --- apple	2026-09-30 17:10:05
← n.	苹果
← [医]	苹果
```

### 6.3 历史（同词多次查询 = 多词头）

```
→ query apple
← ok 2
← --- apple
← n.	苹果
← [医]	苹果

→ query apple
← ok 2
← --- apple
← n.	苹果
← [医]	苹果

→ history
← ok 8
← --- apple	2026-09-30 17:10:05
← n.	苹果
← [医]	苹果
← --- apple	2026-09-30 17:10:03
← n.	苹果
← [医]	苹果
```

**注意**：两次 `query apple` 是两次独立查询（不同 `batch`），**两个词头**。

### 6.4 失败响应

```
→ query zzzzz
← not_found

→ login alice wrong
← wrong_pwd

→ logout          （未登录）
← err not logged in
```

---

## 7. 输入层约定

**客户端输入以 `.` 开头**（`.query apple`）；客户端**去除前导 `.`** 后发送（发 `query apple`）。

- `.` 是**输入友好层**，**不属于协议**。协议本身**不认识带点的命令**。
- 管理终端**同样**：用户输 `.help`，去点后 `decodeSys("help")`。
- **`nc` 等直连测试**：**发不带点**（`query apple`）。

**例**：

| 用户输入 | 线上传输 |
|---------|---------|
| `.query apple` | `query apple\n` |
| `.login alice 123` | `login alice 123\n` |
| `.quit` | `quit\n` |

---

## 8. 编码与边界

- **字符集**：UTF-8
- **行终止**：`\n`（LF）；**不识别** `\r\n`
- **单行上限**：`net::BUF_SIZE = 4096`（单次 `recv` 缓冲）；**行本身可跨多次 `recv` 拼接**（`recvLine` 累积）
- **空行**：客户端不发；服务端 `decode` 返回 `nullopt` → `err bad request`

---

## 9. 扩展约定

新增命令 / 状态词时：

1. **命令**：小写，无点；加入 `UsrCmd` 或 `SysCmd` 枚举，`decode` 映射
2. **状态词**：**仅在客户端需特殊处理时**新增（否则 `err` + `reason`）
3. **多行响应**：沿用 `ok <n>` + `n` 行；分组用 `--- word` 头；纯文本直接逐行
4. **向后兼容**：新增字段放行尾（`\t` 追加）；客户端对未知字段**忽略**，不报错

---

## 10. 与实现的对应

| 协议部分 | 实现 |
|---------|------|
| 编解码 | `common/proto.{hpp,cpp}`（`encode` / `decodeUsr` / `decodeSys` / `makeOk` / `makeErr`） |
| 响应解析 | `proto::isOk` / `respStat` / `respData` / `respReason` |
| 行收发 | `common/net.{hpp,cpp}`（`sendAll` / `recvLine`） |
| 命令枚举 | `common/proto.hpp`（`UsrCmd` / `SysCmd`） |
| 状态枚举 | `common/types.hpp`（`status::UsrOp` / `Query` / `Admin`） |
| 服务端分发 | `ser/ser.cpp`（`handleClient` / `handleSystem`） |
| 客户端渲染 | `cli/cli.cpp`（`handleResp`） |

---

## 附：完整命令一览（速查）

```
[用户]

  .reg   <name> <pwd>          注册
  .login <name> <pwd>          登录
  .logout                      登出
  .help                        指令集
  .quit / .exit                断开

  .query   <word>              查词
  .history [num]               历史（默认 10 次）
  .star    <word>              收藏
  .unstar  <word>              取消收藏
  .pad     [num]               收藏列表（默认 10 词）

[管理终端]

  .list   [name*]              列词
  .view   <name>               查看
  .add    <word> <pos> <mean>  加词
  .del    <name>               删词
  .update <word> <pos> <mean>  改词
  .reload                      重载词库
  .num                         词条数

  .stat    <usrname>           用户信息
  .history <usrname> [num]     用户历史
  .pad     <usrname> [num]     用户收藏
  .log     <level>             日志等级
  .shutdown                    关服务器
  .help                        管理指令集
```