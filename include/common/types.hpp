/// @file common/types.hpp
/// @brief 跨模块通用类型：状态枚举集合（stat::*）与字典词条（Meaning）。
/// 只依赖标准库；被 proto / db / ser / cli 等模块 include。

#pragma once

#include <string>

/// 项目中所有"操作结果状态"的集合。
namespace stat {

    /// 用户操作（注册 / 登录 / 登出）
    enum class UsrOp {
        Ok,             ///< 操作成功
        NotFound,       ///< 用户不存在
        Exists,         ///< 用户名已存在
        WrongPwd,       ///< 密码错误
        Online,         ///< 已在线
        Err,            ///< 执行出错
    };

    /// 查词操作
    enum class Query {
        Ok,             ///< 查询成功执行
        NotFound,       ///< 词未找到
        Err,            ///< 执行出错
    };

    /// 客户端连接状态
    enum class Conn {
        Connected,      ///< 连接态
        Disconnected,   ///< 断连态
    };

    /// 管理终端（cmd）命令执行状态
    enum class Admin {
        Ok,             ///< cmd 成功执行
        BadArgs,        ///< 参数错误
        NotFound,       ///< 指令未找到
        Err,            ///< 执行出错
    };

}   // namespace stat

/// 一条字典释义
struct Meaning {
    long long   rowid;   ///< 数据库行号（对应 SQLite 的 rowid，64 位有符号）
    std::string text;    ///< 释义文本
};