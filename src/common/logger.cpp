/// @file common/logger.cpp
/// @brief 日志模块实现：等级变量与字符串解析。

#include "common/logger.hpp"

#include <cstring>   // std::strcmp

namespace logger {

Level g_level = Level::Info;   // 默认 INFO

bool parseLevel(const char* s, Level& out) {
    if (std::strcmp(s, "debug") == 0) { out = Level::Debug; return true; }
    if (std::strcmp(s, "info")  == 0) { out = Level::Info;  return true; }
    if (std::strcmp(s, "warn")  == 0) { out = Level::Warn;  return true; }
    if (std::strcmp(s, "err")   == 0) { out = Level::Err;   return true; }
    return false;
}

}   // namespace logger