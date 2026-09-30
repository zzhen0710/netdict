/// @file ser/ser.hpp
/// @brief 服务器主类：监听、收连接、处理请求（单线程骨架版）。

#pragma once

#include "ser/thread_pool.hpp"
#include "common/guard/fd_guard.hpp"    // FdGuard
#include "common/net.hpp"               // net::DEFAULT_IP / DEFAULT_PORT
#include "common/proto.hpp"             // proto::Msg / proto::UsrCmd
#include "db/dict_repo.hpp"
#include "db/usr_repo.hpp"
#include "common/line_editor.hpp"   // LineEditor

#include <optional>                 // std::optional
#include <atomic>                   // std::atomic
#include <mutex>                    // std::mutex

#include <unordered_map>            // std::unordered_map

/// 一条连接状态（key 即 fd，故不存 fd 字段）
struct Conn {
    bool in_flight = false;         ///< 是否已派发给工作线程
    std::string usr;                ///< 当前登录用户名（空 = 未登录）
};

/// TCP 服务器：epoll 监听 listen_fd_ + 所有连接 fd；
/// 连接就绪后摘出 epoll，交给线程池阻塞收发处理。
class Server {
public:
    /// 构造：创建 socket、bind、listen，创建 epoll 并把 listen_fd_ 加入。
    /// @throws std::runtime_error socket / bind / listen / epoll 失败
    Server(DictRepo& dict, UsrRepo& usr,
           std::string_view ip = net::DEFAULT_IP,
           int port = net::DEFAULT_PORT);

    /// 析构：成员 RAII 自动清理（FdGuard / ThreadPool / optional<LineEditor>）。
    /// 无手动逻辑，= default。
    ~Server() = default;

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// 请求停止：只置标志（信号 handler 调，async-signal-safe）。
    void requestStop();

    /// 启动：阻塞 epoll_wait 循环，直到 requestStop()。
    void run();

private:
    /// 真正清理：踢连接、停线程池（主循环退出后调，不在信号 handler 里）。
    void doStop();

    // ---- 单个客户端处理 ----

    /// accept 新连接，循环到 EAGAIN；每个新 fd 设非阻塞、ADD 进 epoll、登记 conns_。
    void handleAccept();

    /// 连接就绪：摘除 epoll、标记 in_flight、派发给线程池。
    void handleConn(int fd);

    /// 处理单个客户端：阻塞收发自循环，直到连接关闭。
    /// 结束时 close(fd) + 从 conns_ 摘除（同一临界区）。
    void handleClient(int cfd);

    // ---- 临界区用户会话管理：查 / 记 / 清（操作 conns_[fd].usr） ----

    std::optional<std::string> usrGet(int cfd);
    void usrSet(int cfd, const std::string& name);
    void usrClear(int cfd);

    // ---- 用户命令：分发 + 细粒度实现 ----

    /// 字典：query / history / star / unstar / pad
    void handleUsrDict(int cfd, const proto::Msg& msg, proto::UsrCmd::Dict c);
    void doQuery  (int cfd, const proto::Msg& msg);
    void doHistory(int cfd, const proto::Msg& msg);
    void doStar   (int cfd, const proto::Msg& msg);
    void doUnstar (int cfd, const proto::Msg& msg);
    void doPad    (int cfd, const proto::Msg& msg);

    /// 控制：reg / login / logout / help / quit
    void handleUsrCtrl(int cfd, const proto::Msg& msg, proto::UsrCmd::Ctrl c);
    void doReg   (int cfd, const proto::Msg& msg);
    void doLogin (int cfd, const proto::Msg& msg);
    void doLogout(int cfd, const proto::Msg& msg);
    void doHelp  (int cfd, const proto::Msg& msg);
    void doQuit  (int cfd, const proto::Msg& msg);

    // ---- 管理终端（stdin 命令） ----

    /// 处理一行管理命令：读一行（LineEditor）→ decodeSys → visit 分发。
    void handleSystem();

    /// 管理命令：字典（list/view/add/del/update/reload/num）。
    void handleSysDict(const proto::Msg& msg, proto::SysCmd::Dict c);

    /// 管理命令：控制（stat/history/pad/log/shutdown/help）。
    void handleSysCtrl(const proto::Msg& msg, proto::SysCmd::Ctrl c);

    // ---- 发送工具（带日志） ----

    /// 发送原始字节（不补 \n）；net::sendAll 保证发完整。
    void sendBytes(int cfd, std::string_view data);

    /// 发送一行（自动补 \n）；行协议专用。
    void sendLine(int cfd, std::string_view line);

    /// ---- 依赖（不拥有，引用） ----
    DictRepo& dict_;          ///< 字典数据表
    UsrRepo&  usr_;           ///< 用户数据表
    
    /// ---- 连接状态：cfd → Conn ----
    std::unordered_map<int, Conn> conns_;
    std::mutex conns_mtx_;

    /// ---- 自身状态 ----
    ThreadPool        thread_pool_;    ///< 并发处理客户端
    FdGuard           listen_fd_;      ///< 监听套接字
    FdGuard           epoll_fd_;       ///< epoll 实例
    std::string       ip_;             ///< 监听 IP（string_view 转存）
    int               port_;           ///< 监听端口
    std::atomic<bool> running_{false}; ///< 运行标志（信号 handler 与 run 竞争置 false）

    EditorHistory     editor_history_; ///< 管理终端历史（进程级，构造 Load / 析构 Save）
    std::optional<LineEditor> editor_; ///< 管理终端编辑会话（run 里 emplace，延迟构造）
};