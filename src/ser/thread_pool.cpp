/// @file ser/thread_pool.cpp
/// @brief 线程池实现。

#include "ser/thread_pool.hpp"

#include <stdexcept>   // std::invalid_argument
#include <utility>     // std::move

/// 创建 n 个工作线程。
/// @throws std::invalid_argument n 不在 1~1024。
ThreadPool::ThreadPool(std::size_t n) {
    if (n < MIN_THREADS || n > MAX_THREADS) {
        throw std::invalid_argument("线程数必须在 1~1024");
    }

    workers_.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        // emplace_back 原地构造 std::thread：参数直接转发给 thread 构造，
        // 无临时对象、无 move。&ThreadPool::worker + this = 成员函数 + 对象。
        workers_.emplace_back(&ThreadPool::worker, this);
    }
}

/// 析构：请求停止 + join 全部线程。
///
/// 关键：析构不能"直接销毁线程"（线程是独立执行流），只能 join（阻塞等）。
/// 若只置 stop_ 而不 notify，睡眠中的线程永远等不到唤醒，join 会卡死。
ThreadPool::~ThreadPool() {
    stop();

    for (auto& t : workers_) {
        // 本类不 detach 线程，joinable() 恒为 true；
        // 但若将来允许 detach，对不可 join 的线程 join 会抛异常，故留此防御。
        if (t.joinable()) t.join();
    }
}

/// 提交任务：入队 + 唤醒一个工作线程；已停止则丢弃。
void ThreadPool::addTask(Task task) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        // stop_ 与 tasks_ 同受 mtx_ 保护；锁外读 stop_ 是数据竞争。
        if (stop_) return;
        tasks_.push(std::move(task));
    }
    // notify 放锁外：避免"被唤醒的线程立刻被调度、却因锁仍被本线程占着
    // 而多阻塞一轮"。放锁外，醒来时锁已空，可立即拿锁取任务。
    cv_.notify_one();
}

/// 请求停止：置 stop_、唤醒全部线程；幂等。
void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        stop_ = true;
    }
    // 唤醒全部（不只一个）：可能有多个线程正在 wait。
    cv_.notify_all();
}

/// 工作线程主体：循环取任务、执行，直到 stop_ 且队列为空。
void ThreadPool::worker() {
    // 用 while(true) 而非 while(!stop_)：
    //   stop_ 只代表"收到停止请求"，不代表"立刻退出"。
    //   线程收到停止请求后仍需"清空队列"再退；真正的退出判据
    //   放在 wait 之后（stop_ && tasks_.empty()）。
    //   若外层用 while(!stop_)，stop_ 一置位就跳出，会丢队列中剩余任务。
    while (true) {
        Task task;

        {
            std::unique_lock<std::mutex> lock(mtx_);
            // wait 的谓词含 stop_，否则停止时线程无法被唤醒（死锁）。
            cv_.wait(lock, [this] {
                return stop_ || !tasks_.empty();
            });

            // 要停且队列空 → 退出；否则继续取任务。
            if (stop_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        task();   // 不持锁执行
    }
}