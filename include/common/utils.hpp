/// @file common/utils.hpp
/// @brief 通用工具：时间格式化。
/// 被 cli / ser / db 等模块共用。

#pragma once

#include <ctime>
#include <string>

namespace utils {

    /*
    overloaded 把多个 lambda 继承进来，用 using 把它们的 operator() 拉到同一层构成重载，
    再用推导指引让 overloaded{...} 自动推类型，结果就是一个能接受多种类型的可调用对象，正好满足 std::visit 的要求
    */
    /// 把多个 lambda 合成一个重载集，供 std::visit
    template<class... Ts>
    struct overloaded : Ts... { using Ts::operator()...; };

    /// 推导指引（让 overloaded{a,b} 自动推导）
    template<class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

    /// 时间戳 → "YYYY-MM-DD HH:MM:SS"（线程安全）
    [[nodiscard]] std::string formatTime(std::time_t ts);

    /// 当前时间的格式化字符串
    [[nodiscard]] std::string now();

}   // namespace utils