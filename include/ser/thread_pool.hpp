/// @file ser/thread_pool.hpp
/// @brief 线程池：任务队列 + 工作线程，用于并发处理客户端请求。
/// 仅服务器使用；不依赖网络与数据库。

#pragma once

#include <condition_variable>   // std::condition_variable
#include <cstddef>              // std::size_t
#include <functional>           // std::function
#include <mutex>                // std::mutex, std::lock_guard, std::unique_lock
#include <queue>                // std::queue
#include <thread>               // std::thread
#include <vector>               // std::vector

/// 固定大小线程池：addTask 提交任务，stop 优雅关闭（清空队列再退出）。
class ThreadPool {
public:
    using Task = std::function<void()>;   ///< 任务：无参、返回 void

    /// 创建 n 个工作线程；n 必须在 1~1024。
    /// @throws std::invalid_argument n 越界。
    explicit ThreadPool(std::size_t n = 4);

    /// 析构：内部调用 stop() 后 join 所有线程。
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// 提交任务；已停止则丢弃。
    void addTask(Task task);

    /// 请求停止：置 stop_ 并唤醒全部线程；此后 addTask 一律丢弃。
    /// 幂等；不阻塞（线程退出与 join 由析构完成）。
    void stop();

    /// 工作线程数。
    std::size_t size() const { return workers_.size(); }

private:
    /// 工作线程主体：循环取任务、执行，直到 stop_ 且队列为空。
    void worker();

    std::vector<std::thread> workers_;      ///< 工作线程
    std::queue<Task>         tasks_;        ///< 任务队列（FIFO）
    std::mutex               mtx_;          ///< 保护 tasks_ 与 stop_
    std::condition_variable  cv_;           ///< 唤醒：有新任务 / 要停止
    bool                     stop_ = false; ///< 停止标志
};