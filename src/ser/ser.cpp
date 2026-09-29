/// @file ser/ser.cpp
/// @brief 服务器实现（第一版：accept + 收一行 + 解码分发 + 回一行）。

#include "ser/ser.hpp"
#include "common/logger.hpp"
#include "common/net.hpp"
#include "common/utils.hpp"     // utils::overloaded

#include <arpa/inet.h>      // inet_addr, inet_ntoa
#include <netinet/in.h>     // sockaddr_in
#include <sys/epoll.h>      // epoll_create1, epoll_ctl, epoll_event, EPOLLIN
#include <sys/socket.h>     // socket, bind, listen, accept, setsockopt
#include <fcntl.h>          // fcntl, F_GETFL, F_SETFL, O_NONBLOCK
#include <unistd.h>         // close

#include <cerrno>           // errno, EAGAIN, EWOULDBLOCK, EINTR
#include <cstring>          // std::strerror

// ---- 文件内常量 ----

namespace {
    /// epoll_wait 每轮最多取回的就绪事件数
    constexpr int kMaxEvents = 1024;
}

// ---- 构造 / 析构 ----

/// 构造：创建 socket、bind、listen。
Server::Server(DictRepo& dict, UsrRepo& usr,
               std::string_view ip, int port)
    : dict_(dict), usr_(usr),
      thread_pool_(4),
      ip_(ip), port_(port), running_(false) {   // 列表初始化顺序要和成员变量声明顺序一致

    // 1.创建 TCP 套接字
    {   // 块限制裸 fd 作用域，随后被 RAII 接管
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            throw std::runtime_error("socket failed");
        }
        listen_fd_ = FdGuard(fd);
    }

    // 2. 端口复用：避免 TIME_WAIT 期间无法立即重启
    int opt = 1;
    setsockopt(listen_fd_.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. 监听套接字设非阻塞（handleAccept 循环 accept 到 EAGAIN 需要）
    {
        int flags = fcntl(listen_fd_.get(), F_GETFL, 0);        // 取标志
        if (flags < 0 || fcntl(listen_fd_.get(), F_SETFL, flags | O_NONBLOCK)) {        // 设（追加）标志
            throw std::runtime_error("set listen_fd nonblock failed");
        }
    }

    // 4. 构造本机地址
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    addr.sin_addr.s_addr = inet_addr(ip_.c_str());

    // 5. bind
    if (bind(listen_fd_.get(),
             reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error("bind failed");       
    }

    // 6. listen
    if (listen(listen_fd_.get(), 128) < 0) {
        throw std::runtime_error("listen failed");  
    }

    // 7. 创建 epoll 实例
    {
        int efd = epoll_create1(EPOLL_CLOEXEC);   // 建 epoll 实例；exec 时自动关闭，防 fd 泄漏
        if (efd < 0) {
            throw std::runtime_error("epoll_create1 failed");
        }
        epoll_fd_ = FdGuard(efd);   // 析构自动 close；CLOEXEC 管 exec，两者不冲突
    }

    // 8. 把 listen_fd_ 加入 epoll，监听可读（有新连接）
    {   // 块限制 ev 作用域，只用于注册添加 listen_fd_
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = listen_fd_.get();
        if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD,
                      listen_fd_.get(), &ev) < 0) {
            throw std::runtime_error("epoll_ctl add listen_fd failed");
        }
    }

    LOG_INFO("server listen on %s : %d", ip_.c_str(), port_);
}

/// 析构：FdGuard 自动关闭 listen_fd_ / epoll_fd_。
Server::~Server() {}

// ---- 停止 / 运行 ----

/// 请求停止：只置标志，供信号 handler 调。
/// 信号 handler 里只能调 async-signal-safe 函数；
/// std::atomic<bool> 的 store 在 lock-free 下是安全的。
void Server::requestStop() {
    running_.store(false);
}

void Server::run() {
    running_ = true;                    // 置运行标志，进入主循环

    epoll_event events[kMaxEvents];     // 事件数组，epoll_wait 往里填就绪事件

    // 主循环：原子读，只要 running_ 为真，就一直等事件
    while (running_.load()) {
        // 阻塞等事件；-1 = 不限时
        int n = epoll_wait(epoll_fd_.get(), events, kMaxEvents, -1);
        if (n < 0) {
            if (errno == EINTR) {
                // 被信号打断：继续下一轮，回到 while 判 running_；
                // requestStop() 已把 running_ 置 false，循环退出。
                continue;
            }
            LOG_ERR("epoll_wait failed: %s", std::strerror(errno));
            break;
        }

        // 遍历本次就绪的 n 个事件
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;      // 事件里带回的 fd

            if (fd == listen_fd_.get()) {
                handleAccept();              // 监听 fd：新连接
            } else {
                handleConn(fd);              // 连接就绪：DEL + addTask
            }
        }
    }

    LOG_INFO("server loop exited");
    // 主循环退出：做真正的清理（不在信号 handler 里）
    doStop();
}

/// 真正清理：踢连接、停线程池。主循环退出后调。
void Server::doStop() {
    // 1. shutdown 所有连接，踢出阻塞在 recv 的工作线程
    {
        std::lock_guard lk(conns_mtx_);
        for (auto& pair : conns_) {
            ::shutdown(pair.first, SHUT_RDWR);
        }
    }

    // 2. 通知线程池停止：已入队任务跑完，worker 自然退出
    thread_pool_.stop();

    LOG_INFO("server stopped");
}

//  ---- 单个客户端处理 ----

/// accept 新连接，循环到 EAGAIN；每个新 fd 设非阻塞、ADD 进 epoll、登记 conns_。
void Server::handleAccept() {
    // 循环 accept，直到内核队列空（EAGAIN）
    while (true) {
        sockaddr_in cli{};                  // 用 sockaddr_in（有 sin_addr/sin_port）；传 accept 时转 sockaddr*
        socklen_t cli_len = sizeof(cli);

        // 收一个连接（listen_fd_ 非阻塞）
        int cfd = accept(listen_fd_.get(),
                         reinterpret_cast<sockaddr*>(&cli), &cli_len);

        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;   // 队列空（非阻塞 accept 返回 EAGAIN），收干净
            }
            if (errno == EINTR) {
                continue;                    // 被信号打断，重试
            }
            LOG_ERR("accept failed: %s", std::strerror(errno));
            break;
        }

        LOG_INFO("client connected: %s:%d (fd = %d)",
                 inet_ntoa(cli.sin_addr), ntohs(cli.sin_port), cfd);

        // 登记 + ADD 同一临界区（防"已登记但未 ADD"的窗口）
        {
            std::lock_guard lk(conns_mtx_);  // 锁：保护 conns_

            epoll_event ev{};                // 注册到 epoll
            ev.events = EPOLLIN;
            ev.data.fd = cfd;

            conns_[cfd] = Conn{};            // 登记新连接

            if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, cfd, &ev) < 0) {
                // ADD 失败：回滚登记 + 关 fd，跳过
                LOG_WARN("epoll_ctl ADD fd=%d failed: %s", cfd, std::strerror(errno));
                ::close(cfd);
                conns_.erase(cfd);

                continue;
            }
        }
    }
}

/// 连接就绪：从 epoll 摘除、标记 in_flight、派发给线程池。
void Server::handleConn(int fd) {
    {
        std::lock_guard lk(conns_mtx_);   // 锁：保护 conns_

        // 查 fd；未知或已派发则跳过
        auto it = conns_.find(fd);
        if (it == conns_.end() || it->second.in_flight) {
            return;
        }

        // 标记 in_flight + 摘除 epoll（同一临界区，防重复派发）
        it->second.in_flight = true;
        epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr);
    }

    // 派发：任务丢线程池；失败说明线程池已 stop
    bool ok = thread_pool_.addTask([this, fd] {
        handleClient(fd);
    });

    if (!ok) {
        // 线程池已 stop：就地清理（关 fd、删记录）
        std::lock_guard lk(conns_mtx_);
        ::close(fd);
        conns_.erase(fd);
    }
}

/// 处理单个客户端：阻塞收发自循环，直到连接关闭。
/// 结束时 close(fd) + 从 conns_ 摘除（同一临界区）。
void Server::handleClient(int cfd) {
    // 本连接专用接收缓冲（局部，不共享；防跨连接/跨线程竞争）
    std::string recv_buf;
    std::string line;

    // 逐行收、解析、分发，直到对端关闭
    while (true) {
        // 收一行（net::recvLine：从 recv_buf 切；不足时再 recv）
        if (!net::recvLine(cfd, recv_buf, line)) {
            // 对端关闭：取用户名记日志，退出循环
            if (logger::enabled(logger::Level::Info)) {
                auto name = usrGet(cfd);
                LOG_INFO("peer closed: fd = %d, usr = %s",
                        cfd, name ? name->c_str() : "-");
            }
            break;
        }

        // 收到一行：取用户名记日志（每行重取，会话可能变）
        if (logger::enabled(logger::Level::Info)) {
            auto name = usrGet(cfd);
            const char* who = name ? name->c_str() : "-";
            LOG_INFO("recv: fd = %d, usr = %s, data = %.*s",
                    cfd, who, static_cast<int>(line.size()), line.data());
        }

        // 1. 解析成 Msg；失败回 bad request
        auto msg = proto::decodeUsr(line);
        if (!msg) {
            sendLine(cfd, proto::makeErr("err", "bad request"));
            continue;
        }

        // 2. 按命令类型分发：用户命令处理，系统命令拒绝
        std::visit(utils::overloaded {
            [&](proto::UsrCmd::Dict c) { handleUsrDict(cfd, *msg, c); },
            [&](proto::UsrCmd::Ctrl c) { handleUsrCtrl(cfd, *msg, c); },
            [&](proto::SysCmd::Dict)   { sendLine(cfd, proto::makeErr("err", "forbidden")); },
            [&](proto::SysCmd::Ctrl)   { sendLine(cfd, proto::makeErr("err", "forbidden")); },
        }, msg->cmd);
    }

    // 统一出口：关 fd + 从 conns_ 摘除（同一临界区）
    {
        std::lock_guard lk(conns_mtx_);   // 锁：保护 conns_
        ::close(cfd);
        conns_.erase(cfd);
    }
}

// ---- 临界区会话管理 ----

/// 查该 cfd 的登录用户名；未登录返回 nullopt。
/// 未登录的定义：fd 不在 conns_（连接已不存在），或 usr 为空（连了但没登录 / 已登出）。
std::optional<std::string> Server::usrGet(int cfd) {
    // 锁：conns_ 可能被主线程、工作线程同时访问
    std::lock_guard<std::mutex> lk(conns_mtx_);

    // 查 fd：不在 = 连接已关闭，视为未登录
    auto it = conns_.find(cfd);
    if (it == conns_.end()) {
        LOG_DEBUG("usrGet: fd = %d not found", cfd);
        return std::nullopt;
    }

    // 在但 usr 空 = 连了没登录 / 已登出，也视为未登录
    if (it->second.usr.empty()) {
        LOG_DEBUG("usrGet: fd = %d not logged in", cfd);
        return std::nullopt;
    }

    LOG_DEBUG("usrGet: fd = %d usr = %s", cfd, it->second.usr.c_str());
    return it->second.usr;
}

/// 登录 / 注册成功：记 cfd → name。
void Server::usrSet(int cfd, const std::string& name) {
    // 锁：写 conns_
    std::lock_guard<std::mutex> lk(conns_mtx_);

    // 用 find 而非 operator[]：
    // operator[] 在 key 不存在时会默认构造一个 Conn 插入，凭空造出连接记录，污染 conns_。
    auto it = conns_.find(cfd);
    if (it == conns_.end()) {
        // 连接已不存在（异常路径，比如刚被关闭），忽略
        LOG_WARN("usrSet: fd = %d not found", cfd);
        return;
    }

    it->second.usr = name;
    LOG_DEBUG("usrSet: fd = %d usr = %s", cfd, name.c_str());
}

/// 登出：只清 cfd 的用户名，不删 conns_ 条目。
/// 连接仍活着（还能继续发命令，如再 .login），所以条目必须保留。
void Server::usrClear(int cfd) {
    // 锁：写 conns_
    std::lock_guard<std::mutex> lk(conns_mtx_);

    auto it = conns_.find(cfd);
    if (it != conns_.end()) {
        it->second.usr.clear();
    }

    LOG_DEBUG("usrClear: fd = %d", cfd);
}

// ---- 发送辅助 ----

/// 发送原始字节（不补 \n）；net::sendAll 保证发完整。
void Server::sendBytes(int cfd, std::string_view data) {
    if (net::sendAll(cfd, data.data(), data.size()) < 0) {
        LOG_ERR("send failed");
    }
}

/// 发送一行（自动补 \n）；行协议专用。
void Server::sendLine(int cfd, std::string_view line) {
    // 拼出 "line\n"；std::string 拥有内容，传给 sendAll
    std::string out(line);
    out += '\n';

    if (logger::enabled(logger::Level::Debug)) {
        auto name = usrGet(cfd);
        const char* who = name ? name->c_str() : "-";
        LOG_DEBUG("send: fd = %d, usr = %s, data = %.*s",
                cfd, who, static_cast<int>(line.size()), line.data());
    }
    
    sendBytes(cfd, out);
}