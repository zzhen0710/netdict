/// @file ser/ser.hpp
/// @brief 服务器主类：监听、收连接、处理请求（单线程骨架版）。

#pragma once

#include "common/net.hpp"       // net::DEFAULT_IP / DEFAULT_PORT
#include "common/proto.hpp"     // proto::Msg / proto::UsrCmd
#include "db/dict_repo.hpp"
#include "db/usr_repo.hpp"
#include <string>
#include <string_view>

/// TCP 服务器：单线程 accept 循环，处理完一个客户端再收下一个。
/// 第一版只做"收一行 → 解码 → 分发"；业务在 handleUsrDict / handleUsrCtrl。
class Server {
public:
    /// 构造：创建 socket、bind、listen。
    /// @param dict  字典仓储（外部持有，本类仅引用）
    /// @param usr   用户仓储（外部持有，本类仅引用）
    /// @param ip    监听 IP（"0.0.0.0" = 所有网卡）
    /// @param port  监听端口
    /// @throws std::runtime_error socket / bind / listen 失败
    Server(DictRepo& dict, UsrRepo& usr,
           std::string_view ip = net::DEFAULT_IP,
           int port = net::DEFAULT_PORT);

    /// 析构：关闭监听套接字。
    ~Server();

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    /// 启动：阻塞 accept 循环，直到 stop()。
    void run();

    /// 请求停止（可从信号处理调）。
    void stop();

private:
    /// 处理单个客户端连接：收一行 → decodeUsr → visit 分发 → 回一行。
    void handleClient(int cfd);

    /// 发送原始字节（不补 \n）；net::sendAll 保证发完整。
    void sendBytes(int cfd, std::string_view data);

    /// 发送一行（自动补 \n）；行协议专用。
    void sendLine(int cfd, std::string_view line);

    // ---- 用户命令：分发 + 细粒度实现 ----

    // 用户字典命令：query / history / star / pad
    void handleUsrDict(int cfd, const proto::Msg& msg, proto::UsrCmd::Dict c);
    void doQuery  (int cfd, const proto::Msg& msg);
    void doHistory(int cfd, const proto::Msg& msg);
    void doStar   (int cfd, const proto::Msg& msg);
    void doPad    (int cfd, const proto::Msg& msg);

    // 用户控制命令：reg / login / logout / help
    void handleUsrCtrl(int cfd, const proto::Msg& msg, proto::UsrCmd::Ctrl c);
    void doReg   (int cfd, const proto::Msg& msg);
    void doLogin (int cfd, const proto::Msg& msg);
    void doLogout(int cfd, const proto::Msg& msg);
    void doHelp  (int cfd, const proto::Msg& msg);

    // ---- 依赖（不拥有，引用） ----
    DictRepo& dict_;          ///< 字典数据表
    UsrRepo&  usr_;           ///< 用户数据表

    // ---- 自身状态 ----
    int         listen_fd_;   ///< 监听套接字
    std::string ip_;          ///< 监听 IP（string_view 转存）
    int         port_;        ///< 监听端口
    bool        running_;     ///< 运行标志
};