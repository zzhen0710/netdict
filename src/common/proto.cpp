/// @file common/proto.cpp
/// @brief 协议编解码实现：Cmd/Stat 转字符串、请求编解码、响应构造与解析。

#include "common/proto.hpp"

namespace proto {

    // ---------- 命令 → 字符串 ----------

    /// 把用户字典命令转为小写命令名（如 Query → "query"）。
    std::string_view Cmd2Str(UsrCmd::Dict c) noexcept {
        switch (c) {
            case UsrCmd::Dict::Query:   return "query";
            case UsrCmd::Dict::History: return "history";
            case UsrCmd::Dict::Star:    return "star";
            case UsrCmd::Dict::Pad:     return "pad";
        }
        return "unknown";
    }

    /// 把用户控制命令转为小写命令名（如 Reg → "reg"）。
    std::string_view Cmd2Str(UsrCmd::Ctrl c) noexcept {
        switch (c) {
            case UsrCmd::Ctrl::Reg:    return "reg";
            case UsrCmd::Ctrl::Login:  return "login";
            case UsrCmd::Ctrl::Logout: return "logout";
            case UsrCmd::Ctrl::Help:   return "help";
        }
        return "unknown";
    }

    /// 把管理端字典命令转为小写命令名（如 List → "list"）。
    std::string_view Cmd2Str(SysCmd::Dict c) noexcept {
        switch (c) {
            case SysCmd::Dict::List:   return "list";
            case SysCmd::Dict::View:   return "view";
            case SysCmd::Dict::Add:    return "add";
            case SysCmd::Dict::Del:    return "del";
            case SysCmd::Dict::Update: return "update";
            case SysCmd::Dict::Reload: return "reload";
            case SysCmd::Dict::Num:    return "num";
        }
        return "unknown";
    }

    /// 把管理端控制命令转为小写命令名（如 Stat → "stat"）。
    std::string_view Cmd2Str(SysCmd::Ctrl c) noexcept {
        switch (c) {
            case SysCmd::Ctrl::Stat:    return "stat";
            case SysCmd::Ctrl::History: return "history";
            case SysCmd::Ctrl::Pad:     return "pad";
            case SysCmd::Ctrl::Help:    return "help";
            case SysCmd::Ctrl::Log:     return "log";
        }
        return "unknown";
    }

    // ---------- stat 枚举 → 响应字符串 ----------

    /// 用户操作状态 → 响应首词（如 Ok → "ok"）。
    std::string_view Stat2Str(stat::UsrOp s) noexcept {
        switch (s) {
            case stat::UsrOp::Ok:       return "ok";
            case stat::UsrOp::NotFound: return "not_found";
            case stat::UsrOp::Exists:   return "exists";
            case stat::UsrOp::WrongPwd: return "wrong_pwd";
            case stat::UsrOp::Online:   return "online";
        }
        return "err";
    }

    /// 查询状态 → 响应首词。
    std::string_view Stat2Str(stat::Query s) noexcept {
        switch (s) {
            case stat::Query::Ok:       return "ok";
            case stat::Query::NotFound: return "not_found";
        }
        return "err";
    }

    /// 管理端命令状态 → 响应首词。
    std::string_view Stat2Str(stat::Admin s) noexcept {
        switch (s) {
            case stat::Admin::Ok:       return "ok";
            case stat::Admin::BadArgs:  return "bad_args";
            case stat::Admin::NotFound: return "not_found";
        }
        return "err";
    }

    // ---------- 请求编码 ----------

    /// Msg → 字符串（不含 \n）：命令名 + 逐个空格分隔的参数。
    std::string encode(const Msg& msg) {
        // visit 取出命令名；四类命令统一由 Cmd2Str 处理
        std::string_view name = std::visit([](auto&& c){
            return Cmd2Str(c);
        }, msg.cmd);

        // 命令名 + 参数
        std::string result(name);
        for (const auto& arg : msg.args) {
            result += ' ';
            result += arg;
        }
        return result;
    }

    // ---------- 请求解码 ----------

    /// 按空格切词；连续空格视为一个分隔，跳过前后空格。
    static std::vector<std::string_view> split(std::string_view s) {
        std::vector<std::string_view> words;
        size_t i = 0;

        while (i < s.size()) {
            i = s.find_first_not_of(' ', i);    // 跳过空格，指向首个非空格
            if (i == std::string_view::npos) break;

            size_t j = s.find(' ', i);          // 指向下一个空格
            if (j == std::string::npos) {       // 已到尾部：最后一段
                words.push_back(s.substr(i));
                break;
            }
            words.push_back(s.substr(i, j - i));
            i = j + 1;
        }
        return words;
    }

    /// 解析客户端（UsrCmd）请求：切词 → 匹配命令名 → 构造 Msg；失败返回 nullopt。
    std::optional<Msg> decodeUsr(std::string_view line) {
        // 切词
        std::vector<std::string_view> words = split(line);
        if (words.empty()) return std::nullopt;

        std::optional<Cmd> cmd;
        std::string_view name = words[0];

        // 命令名 → 枚举（UsrCmd 两类合并查）
        if (name == "query")        cmd = UsrCmd::Dict::Query;
        else if (name == "history") cmd = UsrCmd::Dict::History;
        else if (name == "star")    cmd = UsrCmd::Dict::Star;
        else if (name == "pad")     cmd = UsrCmd::Dict::Pad;

        else if (name == "reg")     cmd = UsrCmd::Ctrl::Reg;
        else if (name == "login")   cmd = UsrCmd::Ctrl::Login;
        else if (name == "logout")  cmd = UsrCmd::Ctrl::Logout;
        else if (name == "help")    cmd = UsrCmd::Ctrl::Help;

        else return std::nullopt;

        // 参数：words[1..]，拷贝为 std::string
        std::vector<std::string> args(words.begin() + 1, words.end());
        return Msg{ *cmd, std::move(args) };
    }

    /// 解析管理端（SysCmd）请求：切词 → 匹配命令名 → 构造 Msg；失败返回 nullopt。
    std::optional<Msg> decodeSys(std::string_view line) {
        std::vector<std::string_view> words = split(line);
        if (words.empty()) return std::nullopt;

        std::optional<Cmd> cmd;
        std::string_view name = words[0];

        // 命令名 → 枚举（SysCmd 两类合并查）
        if (name == "list")         cmd = SysCmd::Dict::List;
        else if (name == "view")    cmd = SysCmd::Dict::View;
        else if (name == "add")     cmd = SysCmd::Dict::Add;
        else if (name == "del")     cmd = SysCmd::Dict::Del;
        else if (name == "update")  cmd = SysCmd::Dict::Update;
        else if (name == "reload")  cmd = SysCmd::Dict::Reload;
        else if (name == "num")     cmd = SysCmd::Dict::Num;

        else if (name == "stat")    cmd = SysCmd::Ctrl::Stat;
        else if (name == "history") cmd = SysCmd::Ctrl::History;
        else if (name == "pad")     cmd = SysCmd::Ctrl::Pad;
        else if (name == "help")    cmd = SysCmd::Ctrl::Help;
        else if (name == "log")     cmd = SysCmd::Ctrl::Log;

        else return std::nullopt;

        std::vector<std::string> args(words.begin() + 1, words.end());
        return Msg{ *cmd, std::move(args) };
    }

    // ---------- 响应 ----------

    /// 判断响应首词是否为 "ok"。
    bool isOk(std::string_view line) noexcept {
        auto pos = line.find(' ');
        std::string_view head = (pos == std::string_view::npos)
                                ? line : line.substr(0, pos);
        return head == "ok";
    }

    /// 取响应首词（状态名，如 "ok" / "not_found" / "err"）。
    std::string_view respStat(std::string_view line) noexcept {
        auto pos = line.find(' ');
        return (pos == std::string_view::npos) ? line : line.substr(0, pos);
    }

    /// 取成功响应的数据部分（"ok " 之后）；非 ok 响应或无数据返回空。
    std::string_view respData(std::string_view line) noexcept {
        if (!isOk(line))      return "";
        if (line.size() <= 3) return "";   // "ok" 或 "ok " 本身
        return line.substr(3);
    }

    /// 构造成功响应："ok [data]"；data 为空时为 "ok"。
    std::string makeOk(std::string_view data) {
        if (data.empty()) return "ok";
        return std::string("ok ") + std::string(data);
    }

    /// 构造失败响应："<stat_name> [reason]"；reason 为空时只返回 stat_name。
    std::string makeErr(std::string_view stat_name, std::string_view reason) {
        if (reason.empty()) return std::string(stat_name);
        return std::string(stat_name) + " " + std::string(reason);
    }

}   // namespace proto