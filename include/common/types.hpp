/// @file common/types.hpp
/// @brief 跨模块通用类型：状态枚举集合（status::*）与字典词条（Meaning）。
/// 只依赖标准库；被 proto / db / ser / cli 等模块 include。

#pragma once

#include <cstdint>   // uint8_t
#include <string>    // std::string

namespace status {

    /// 用户操作（注册 / 登录 / 登出）
    enum class UsrOp : uint8_t {
        Ok,             ///< 操作成功
        NotFound,       ///< 用户不存在
        Exists,         ///< 用户名已存在
        WrongPwd,       ///< 密码错误
        Err,            ///< 其他错误
    };

    /// 查词操作
    enum class Query : uint8_t {
        Ok,             ///< 查询成功
        NotFound,       ///< 词未找到
        Starred,        ///< 已经收藏
        Unstarred,      ///< 尚未收藏
        Err,            ///< 其他错误
    };

    /// 管理终端（cmd）命令执行状态
    enum class Admin : uint8_t {
        Ok,             ///< cmd 成功执行
        BadArgs,        ///< 参数错误
        NotFound,       ///< 指令未找到
        Err,            ///< 其他错误
    };

}   // namespace status

/// 一条字典释义
struct Meaning {
    long long   rowid;   ///< 数据库行号（对应 SQLite 的 rowid，64 位有符号）
    std::string text;    ///< 释义文本
};