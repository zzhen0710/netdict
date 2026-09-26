#  netdict — 网络英语字典

# CMake 最低版本要求（3.12 CMAKE_CXX_STANDARD）
cmake_minimum_required(VERSION 3.12)

# 项目名 + 语言（CXX 表示只启用 C++）
project(netdict CXX)

# ---------- C++ 标准 ----------
# 使用 C++17
set(CMAKE_CXX_STANDARD 17)
# 若编译器不支持 C++17，则报错而非静默降级
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 关闭 GCC 扩展，使用纯 ISO C++（可移植性更好）
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------- 编译选项 ----------
# Debug 模式或未指定构建类型时，打开常见警告
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR NOT CMAKE_BUILD_TYPE)
    add_compile_options(-Wall -Wextra)
endif()

# ---------- 头文件路径 ----------
include_directories(${CMAKE_SOURCE_DIR}/include)

# ---------- 依赖库 ----------
# 查找 SQLite3（提供 SQLite::SQLite3 目标）
find_package(SQLite3 REQUIRED)
# 查找线程库（提供 Threads::Threads 目标）
find_package(Threads REQUIRED)

# ---------- 服务器可执行文件 ----------
add_executable(netdict_server
    src/ser/ser_main.cpp        # 服务器入口 main
    src/ser/server.cpp          # 服务器主类：监听、分发
    src/ser/cmd.cpp             # 本地管理终端
    src/ser/thread_pool.cpp     # 线程池
    src/db/dict_repo.cpp        # 字典仓储
    src/db/usr_repo.cpp         # 用户仓储
    src/common/utils.cpp        # 通用工具
)

# 服务器链接：SQLite3 + 线程库
target_link_libraries(netdict_server PRIVATE
    SQLite::SQLite3
    Threads::Threads
)

# ---------- 客户端可执行文件 ----------
add_executable(netdict_client
    src/cli/cli_main.cpp        # 客户端入口 main
    src/cli/cli.cpp             # 客户端逻辑
    src/common/utils.cpp        # 通用工具
)


target_link_libraries(netdict_client PRIVATE
    Threads::Threads
)