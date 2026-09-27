/// @file common/utils.hpp
/// @brief 通用工具：时间格式化。
/// 被 cli / ser / db 等模块共用。

#pragma once

#include <ctime>
#include <string>

namespace utils {

/// 时间戳 → "YYYY-MM-DD HH:MM:SS"（线程安全）
[[nodiscard]] std::string formatTime(std::time_t ts);

/// 当前时间的格式化字符串
[[nodiscard]] std::string now();

}   // namespace utils