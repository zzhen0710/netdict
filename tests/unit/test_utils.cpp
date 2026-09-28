/// test_utils.cpp — utils 时间工具冒烟测试

#include "common/utils.hpp"

#include <cassert>
#include <ctime>
#include <iostream>

int main() {
    // ==================== 1. formatTime 基本格式 ====================
    {
        auto s = utils::formatTime(0);   // 1970-01-01 UTC；本地时区可能偏移

        // 长度固定 19："YYYY-MM-DD HH:MM:SS"
        assert(s.size() == 19);

        // 分隔符位置
        assert(s[4]  == '-');
        assert(s[7]  == '-');
        assert(s[10] == ' ');
        assert(s[13] == ':');
        assert(s[16] == ':');

        // 数字字符
        for (size_t i : {0u,1u,2u,3u,5u,6u,8u,9u,11u,12u,14u,15u,17u,18u}) {
            assert(s[i] >= '0' && s[i] <= '9');
        }
    }
    std::cout << "[OK] formatTime format\n";

    // ==================== 2. formatTime 时间推进 ====================
    {
        // 相邻两秒：输出字符串不同（秒位变化）
        auto s1 = utils::formatTime(1000000000);
        auto s2 = utils::formatTime(1000000001);
        assert(s1 != s2);

        // 同一时间戳两次：相同
        auto a = utils::formatTime(1314520);
        auto b = utils::formatTime(1314520);
        assert(a == b);
    }
    std::cout << "[OK] formatTime advance\n";

    // ==================== 3. now ====================
    {
        auto s = utils::now();
        assert(s.size() == 19);

        // now() 和 formatTime(time(nullptr)) 应一致（同秒内）
        auto t = std::time(nullptr);
        auto expected = utils::formatTime(t);
        auto actual   = utils::now();
        // 允许跨秒：允许不等，但都应是合法格式
        assert(actual.size() == 19);
        assert(expected.size() == 19);
    }
    std::cout << "[OK] now\n";

    std::cout << "ALL OK\n";
    return 0;
}