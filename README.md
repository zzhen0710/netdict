# netdict

基于 TCP 的网络英语字典：客户端发送查询请求，服务器从 SQLite 词库返回释义。

## 状态

骨架阶段。目录结构已确定，以下各节均为预设，业务待实现。

## 目录结构

```
netdict/
├── CMakeLists.txt
├── README.md
├── data/           # 数据文件（dict.txt 进 Git，*.db 不进）
├── docs/           # 设计文档
├── include/        # 对外接口，与 src/ 一一对应
│   ├── cli/        # 客户端
│   ├── common/     # 客户端与服务器共用
│   ├── db/         # 数据访问层
│   └── ser/        # 服务器
├── src/            # 实现
└── build/          # 构建产物（不进 Git）
```

## 模块职责（预设）

| 模块 | 职责 |
|------|------|
| cli | 客户端：连接、发送请求、显示响应 |
| common/proto | 协议定义与编解码 |
| common/types | 跨模块通用类型与常量 |
| common/utils | 通用工具函数 |
| db/db_guard | RAII 封装 sqlite3 资源 |
| db/dict_repo | 字典表访问 |
| db/usr_repo | 用户表与历史表访问 |
| ser/ser | 网络主循环、连接管理 |
| ser/thread_pool | 并发处理请求 |
| ser/cmd | 服务器本地管理终端 |

## 依赖方向（预设）

```
cli ──▶ common
ser ──▶ common + db
db  ──▶ common
```

## 编译（预设）

```bash
mkdir -p build && cd build
cmake ..
make
```

生成两个可执行文件：`netdict_server`、`netdict_client`。

## 运行（预设）

```bash
# 服务器
./netdict_server 0.0.0.0 13140

# 客户端
./netdict_client 127.0.0.1 13140
```

## 文档（预设）

- [架构说明](docs/arch.md)
- [协议说明](docs/proto.md)
- [部署说明](docs/deploy.md)