/// @file common/logger.hpp
/// @brief 日志模块：等级、宏。被所有模块使用。
/// 提供宏 LOG_DEBUG / LOG_INFO / LOG_WARN / LOG_ERR。

#pragma once

#include <cstdio>   // fprintf, stderr（宏里用）

namespace logger {

    /// 日志等级（从细到粗）
    enum class Level {
        Debug,
        Info,
        Warn,
        Err,
    };

    /// 全局等级；低于此等级的日志被丢弃
    extern Level g_level;

    /// 设置等级
    inline void setLevel(Level lv) noexcept {
        g_level = lv;
    }

    /// 运行期判：该级别是否输出（宏内部也用；调用者可据此跳过"为日志的取数"）。
    inline bool enabled(Level lv) noexcept {
        return g_level <= lv;
    }

    /// 解析字符串（如 "debug" / "info"）→ 等级；失败返回 false
    bool parseLevel(const char* s, Level& out);

}   // namespace logger


// ---------- 日志宏 ----------
// 用法：
//   LOG_DEBUG("recv %d bytes", n);
//   LOG_ERR("sqlite error: %s", sqlite3_errmsg(db));

#define LOG_DEBUG(fmt, ...) \
    do { if (logger::g_level <= logger::Level::Debug) \
        fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); } while (0)

#define LOG_INFO(fmt, ...) \
    do { if (logger::g_level <= logger::Level::Info) \
        fprintf(stderr, "[INFO] " fmt "\n", ##__VA_ARGS__); } while (0)

#define LOG_WARN(fmt, ...) \
    do { if (logger::g_level <= logger::Level::Warn) \
        fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__); } while (0)

#define LOG_ERR(fmt, ...) \
    do { fprintf(stderr, "[ERR] " fmt "\n", ##__VA_ARGS__); } while (0)
