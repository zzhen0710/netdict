/// @file common/logger.cpp
/// @brief 日志模块实现：等级、字符串解析、输出文件管理。

#include "common/logger.hpp"

#include <cstring>   // std::strcmp

namespace logger {

    // 默认 INFO
    Level g_level = Level::Info;

    // 日志输出文件；nullptr = 用 stderr（默认）。
    // setFile 打开文件后指向它；close 关闭并重置为 nullptr。
    static FILE* g_file = nullptr;

    /// 解析等级名 → Level；失败返回 false。
    bool parseLevel(const char* s, Level& out) {
        if (std::strcmp(s, "debug") == 0) { out = Level::Debug; return true; }
        if (std::strcmp(s, "info")  == 0) { out = Level::Info;  return true; }
        if (std::strcmp(s, "warn")  == 0) { out = Level::Warn;  return true; }
        if (std::strcmp(s, "err")   == 0) { out = Level::Err;   return true; }
        if (std::strcmp(s, "off")   == 0) { out = Level::Off;   return true; }
        return false;
    }

    /// 设置输出文件。
    /// path == nullptr   → 恢复默认（stderr）
    /// path == "stderr"  → 用标准错误
    /// 其他              → 追加打开文件
    bool setFile(const char* path) {
        close();                                  // 先关旧文件

        if (path == nullptr) return true;         // 恢复默认
        if (std::strcmp(path, "stderr") == 0) return true;

        g_file = std::fopen(path, "a");           // "a" 追加
        if (g_file) {
            // 文件 I/O 默认"全缓冲"：日志攒在内存，攒满才写盘，
            // 服务器还在跑时 cat 文件是空的。
            // 改"行缓冲"（_IOLBF）：每写一行（遇 '\n'）就刷进文件，
            // 日志及时可见，崩溃时也几乎不丢。
            std::setvbuf(g_file, nullptr, _IOLBF, 0);
        }

        return g_file != nullptr;
    }

    /// 当前输出 FILE*；未打开时返回 stderr。
    FILE* file() noexcept {
        return g_file ? g_file : stderr;
    }

    /// 关闭日志文件；幂等。
    void close() noexcept {
        if (g_file) {
            std::fclose(g_file);
            g_file = nullptr;
        }
    }

}   // namespace logger