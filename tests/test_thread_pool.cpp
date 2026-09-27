/// test_thread_pool.cpp — ThreadPool 冒烟测试

#include "ser/thread_pool.hpp"

#include <atomic>      // std::atomic, load, compare_exchange_weak
#include <cassert>     // assert
#include <chrono>      // std::chrono::steady_clock, seconds, milliseconds
#include <iostream>    // std::cout
#include <thread>      // std::this_thread::sleep_for

int main() {
    // ==================== 1. 基本提交与执行 ====================
    {
        ThreadPool pool(4);

        std::atomic<int> counter{0};        // 原子读值
        for (int i = 0; i < 100; ++i) {
            pool.addTask([&counter] { ++counter; });
        }

        // 设 2 秒截止时间，避免任务没跑完时测试无限等待
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

        // 轮询：计数到 100 或超时即退出；每 5ms 查一次
        while (counter.load() < 100 &&          // 显式原子地读取 counter 当前值，返回 int
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(counter.load() == 100);
    }
    std::cout << "[OK] basic submit\n";

    // ==================== 2. 多线程并行 ====================
    {
        ThreadPool pool(4);

        std::atomic<int> running{0};      // 当前正在执行的任务数
        std::atomic<int> peak{0};         // 历史上 running 达到过的最大值，峰值（应 > 1）
        std::atomic<int> done{0};         // 已完成的任务数

        for (int i = 0; i < 8; ++i) {
            pool.addTask([&] {
                int now = ++running;      // 当前同时运行数（并发数）
                int cur = peak.load();    // 当前记录的峰值，无锁更新峰值：只在新值更大时尝试 CAS
                // if (now > cur) 准备更新
                // if (peak == cur) peak = now; return true; 退出循环
                // if (peak != cur) peak 在被别的 thread 修改 -> cur = peak; 记录新值, return false; 继续循环
                while (now > cur && !peak.compare_exchange_weak(cur, now)) {}

                std::this_thread::sleep_for(std::chrono::milliseconds(50));     // 模拟耗时工作
                --running;
                ++done;                   // 表示任务完成，线程沉睡，等待回循环顶端被唤醒
            });
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);     // 3s截止放卡死
        while (done.load() < 8 &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));  // 主线程任务分配完毕，每 5ms 轮询任务任务是否执行完毕
        }
        assert(done.load() == 8);         // 任务已执行完成
        assert(peak.load() > 1);          // 确实并行
    }
    std::cout << "[OK] parallel\n";

    // ==================== 3. 优雅关闭：析构时队列非空，剩余任务仍跑完 ====================
    {
        std::atomic<int> counter{0};
        {
            ThreadPool pool(2);
            // 提交 20 个慢任务，然后立刻析构
            for (int i = 0; i < 20; ++i) {
                pool.addTask([&counter] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    ++counter;
                });
            }
            // 不等任务跑完，直接出作用域
        }
        // 析构应阻塞到"队列清空"才返回
        assert(counter.load() == 20);
    }
    std::cout << "[OK] graceful shutdown\n";

    // ==================== 4. stop 后 addTask 丢弃 ====================
    {
        ThreadPool pool(2);

        std::atomic<int> counter{0};
        pool.addTask([&counter] { ++counter; });

        // 等第一个任务跑完
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (counter.load() < 1 &&
            std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(counter.load() == 1);

        // 请求停止；之后提交的任务应被丢弃
        pool.stop();
        pool.addTask([&counter] { ++counter; });
        pool.addTask([&counter] { ++counter; });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(counter.load() == 1);   // 仍是 1

        assert(pool.size() == 2);      // 线程未 join，size 不变
    }
    std::cout << "[OK] stop / size\n";

    std::cout << "ALL OK\n";
    
    return 0;
}