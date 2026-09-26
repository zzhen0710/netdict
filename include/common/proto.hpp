/// @file common/proto.hpp
/// @brief 协议定义：消息类型、字段、编解码（encode / decode）。
/// 客户端与服务器共用；只处理传输，不涉及业务逻辑。

#pragma once

#include "common/types.hpp"
#include <cstdint>             // uint8_t
#include <optional>            // std::optional
#include <string>              // std::string
#include <string_view>         // std::string_view
#include <variant>             // std::variant, std::visit
#include <vector>              // std::vector

namespace proto {

    // ---------- 命令定义 ----------

    /// 客户端（普通用户）能发的命令
    namespace UsrCmd {
        /// 用户操作字典
        enum class Dict : uint8_t {
            Query,      ///< .query <word>             查单词
            History,    ///< .history [num]            查自己的历史
            Star,       ///< .star <word>              收藏单词
            Pad,        ///< .pad                      显示收藏单词（字母序）
        };
        /// 用户控制
        enum class Ctrl : uint8_t {
            Reg,        ///< .reg <name> <pwd>    注册
            Login,      ///< .login <name> <pwd>  登录
            Logout,     ///< .logout / .quit / .exit  登出
            Help,       ///< .help                欢迎与指令集
        };
    }

    /// 服务器本地管理终端（cmd）命令
    namespace SysCmd {
        /// 管理字典
        enum class Dict : uint8_t {
            List,       ///< .list [name*]        列词（* 通配）
            View,       ///< .view <name>         查看词
            Add,        ///< .add <name> <mean>   加词
            Del,        ///< .del <name>          删词
            Update,     ///< .update <name> <mean>  改词
            Reload,     ///< .reload              重载词库
            Num,        ///< .num                 词条数
        };
        /// 服务器控制
        enum class Ctrl : uint8_t {
            Stat,       ///< .stat [usrname]           打印用户信息
            History,    ///< .history <usrname> [num]  看指定用户历史
            Pad,        ///< .pad <usrname>            看用户单词本
            Help,       ///< .help                     指令集
            Log,        ///< .log <level>              切日志等级
        };
    }

    /// 一条命令：四类枚举之一
    using Cmd = std::variant<
        UsrCmd::Dict,
        UsrCmd::Ctrl,
        SysCmd::Dict,
        SysCmd::Ctrl
    >;

    // ---------- 消息结构 ----------

    /// 一条消息：命令 + 参数列表
    struct Msg {
        Cmd cmd = UsrCmd::Dict::Query;    ///< 命令（默认占位）
        std::vector<std::string> args;    ///< 参数列表
    };

    /// 命令 → 字符串：返回不带点的纯命令名（如 "query"）
    [[nodiscard]] std::string_view Cmd2Str(UsrCmd::Dict c) noexcept;
    [[nodiscard]] std::string_view Cmd2Str(UsrCmd::Ctrl c) noexcept;
    [[nodiscard]] std::string_view Cmd2Str(SysCmd::Dict c) noexcept;
    [[nodiscard]] std::string_view Cmd2Str(SysCmd::Ctrl c) noexcept;

    /// stat 枚举 → 响应首词（如 Ok → "ok"）
    [[nodiscard]] std::string_view Stat2Str(stat::UsrOp s) noexcept;
    [[nodiscard]] std::string_view Stat2Str(stat::Query s) noexcept;
    [[nodiscard]] std::string_view Stat2Str(stat::Admin s) noexcept;

    /// 请求编解码：encode/decode 均不含 \n
    [[nodiscard]] std::string encode(const Msg& msg);
    [[nodiscard]] std::optional<Msg> decodeUsr(std::string_view line);
    [[nodiscard]] std::optional<Msg> decodeSys(std::string_view line);

    /// 响应：成功 "ok [data]"，失败 "<stat_name> [reason]"
    [[nodiscard]] bool isOk(std::string_view line) noexcept;
    [[nodiscard]] std::string_view respStat(std::string_view line) noexcept;
    [[nodiscard]] std::string_view respData(std::string_view line) noexcept;
    [[nodiscard]] std::string makeOk(std::string_view data = "");
    [[nodiscard]] std::string makeErr(std::string_view stat_name,
                                      std::string_view reason = "");

}   // namespace proto