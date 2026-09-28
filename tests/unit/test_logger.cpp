/// test_logger.cpp — logger 冒烟测试

#include "common/logger.hpp"

#include <cassert>
#include <iostream>

int main() {
    // ==================== 1. parseLevel ====================
    {
        logger::Level lv;

        assert(logger::parseLevel("debug", lv) && lv == logger::Level::Debug);
        assert(logger::parseLevel("info",  lv) && lv == logger::Level::Info);
        assert(logger::parseLevel("warn",  lv) && lv == logger::Level::Warn);
        assert(logger::parseLevel("err",   lv) && lv == logger::Level::Err);

        // 非法输入
        assert(!logger::parseLevel("xxx", lv));
        assert(!logger::parseLevel("",    lv));
        assert(!logger::parseLevel("INFO", lv));   // 大小写敏感
    }
    std::cout << "[OK] parseLevel\n";

    // ==================== 2. setLevel / g_level ====================
    {
        assert(logger::g_level == logger::Level::Info);   // 默认 INFO

        logger::setLevel(logger::Level::Warn);
        assert(logger::g_level == logger::Level::Warn);

        logger::setLevel(logger::Level::Debug);
        assert(logger::g_level == logger::Level::Debug);

        logger::setLevel(logger::Level::Info);            // 还原
    }
    std::cout << "[OK] setLevel\n";

    // ==================== 3. 宏（肉眼验证格式 + 等级过滤） ====================
    // 期望输出：
    //   [INFO] info msg
    //   [WARN] warn msg
    //   [ERR] err msg
    // 不输出 DEBUG（当前等级 Info）
    {
        logger::setLevel(logger::Level::Info);

        LOG_DEBUG("debug msg (should NOT appear)");
        LOG_INFO ("info msg");
        LOG_WARN ("warn msg");
        LOG_ERR  ("err msg");

        // 带格式参数
        LOG_INFO("int = %d str = %s", 42, "hello");
    }
    std::cout << "[OK] macros (see above)\n";

    // ==================== 4. DEBUG 等级下宏全开 ====================
    {
        logger::setLevel(logger::Level::Debug);
        LOG_DEBUG("debug msg (should appear)");
        logger::setLevel(logger::Level::Info);   // 还原
    }
    std::cout << "[OK] debug level\n";

    std::cout << "ALL OK\n";
    return 0;
}