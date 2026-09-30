/// @file common/utils.cpp
/// @brief 通用工具实现：时间格式化。

#include "common/utils.hpp"
#include <iostream>

namespace utils {

    /// 时间戳 → "YYYY-MM-DD HH:MM:SS"（线程安全）。
    std::string formatTime(std::time_t ts) {
        // localtime_r 是线程安全版：结果写入调用者提供的 tm_buf，
        // 不像 localtime 返回"内部静态缓冲区"（多线程会互相覆盖）。
        struct tm tm_buf{};
        localtime_r(&ts, &tm_buf);

        // strftime 按格式串输出；%Y-%m-%d %H:%M:%S 即 "年-月-日 时:分:秒"。
        // buf 要足够大：最坏情况长度远超 20 字符，32 是常见保守值。
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

        return buf;   // std::string 从 C 字符串构造
    }

    /// 当前时间的格式化字符串。
    std::string now() {
        // time(nullptr) 返回当前 Unix 时间戳（自 1970-01-01 UTC 起秒数）
        return formatTime(std::time(nullptr));
    }

    /// 打印一行到 stdout。
    /// 按长度写出（可能不含 '\0'，不依赖 '\0'，避免越界或截断），末尾补 '\n'。
    //  必须用 write（不是 <<）：<< 依赖 '\0' 终止，而数据按长度给出。
   void printLine(std::string_view s) {
        std::cout.write(s.data(), static_cast<std::streamsize>(s.size()));
        // 末尾补 '\n'，和上面一致
        std::cout.write("\n", 1);
    }

}   // namespace utils