/// @file common/logger.hpp
/// @brief 日志模块：等级、宏。被所有模块使用。
/// 提供宏 LOG_DEBUG / LOG_INFO / LOG_WARN / LOG_ERR。
///
/// 输出到文件（默认 stderr），避免和终端 prompt 混。
/// 用 logger::setFile(path) 改输出目标；"stderr" 表示标准错误。

#pragma once

#include <cstdio>   // FILE, fopen, fclose, fprintf

namespace logger {

    /// 日志等级（从细到粗；Off = 全关）
    ///
    /// 语义：g_level 是"输出阈值"，低于它的日志被丢弃。
    ///   例：g_level = Warn → 只输出 Warn / Err，Debug / Info 丢弃。
    /// Off 是特殊值：表示"全关"，任何级别都不输出。
    enum class Level {
        Debug,      ///< 最细（调试）
        Info,       ///< 常规信息
        Warn,       ///< 警告
        Err,        ///< 错误
        Off,        ///< 全关
    };

    /// 全局等级；低于此等级的日志被丢弃
    extern Level g_level;

    /// 设置等级
    inline void setLevel(Level lv) noexcept {
        g_level = lv;
    }

    /// 运行期判：该级别是否输出。
    /// Off 时任何级别都 false（Off 不是"可输出级别"，是"关闭"）。
    /// 调用者可据此跳过"为日志取数"的开销。
    inline bool enabled(Level lv) noexcept {
        return lv != Level::Off && g_level <= lv;
    }

    /// 解析字符串（如 "debug" / "info" / "off"）→ 等级；失败返回 false
    bool parseLevel(const char* s, Level& out);

    /// 设置日志输出文件。
    /// @param path  文件路径（追加写）；"stderr" = 标准错误；
    ///               nullptr = 恢复默认（stderr）
    /// @return 成功 true / 打开文件失败 false
    bool setFile(const char* path);

    /// 获取当前输出 FILE*（供日志宏内部用）。
    /// 未 setFile 或已 close 时返回 stderr。
    FILE* file() noexcept;

    /// 关闭日志文件（退出时调；幂等）。
    void close() noexcept;

}   // namespace logger


// ---------- 日志宏 ----------
// 用法：
//   LOG_DEBUG("recv %d bytes", n);
//   LOG_ERR("sqlite error: %s", sqlite3_errmsg(db));
//
// g_level = Off 时，g_level <= Level::Debug 为 false（Off 是最大值），
// 所有宏自动不输出。

#define LOG_DEBUG(fmt, ...) \
    do { if (logger::g_level <= logger::Level::Debug) \
        fprintf(logger::file(), "[DEBUG] " fmt "\n", ##__VA_ARGS__); } while (0)

#define LOG_INFO(fmt, ...) \
    do { if (logger::g_level <= logger::Level::Info) \
        fprintf(logger::file(), "[INFO] " fmt "\n", ##__VA_ARGS__); } while (0)

#define LOG_WARN(fmt, ...) \
    do { if (logger::g_level <= logger::Level::Warn) \
        fprintf(logger::file(), "[WARN] " fmt "\n", ##__VA_ARGS__); } while (0)

#define LOG_ERR(fmt, ...) \
    do { if (logger::g_level <= logger::Level::Err) \
        fprintf(logger::file(), "[ERR] " fmt "\n", ##__VA_ARGS__); } while (0)
