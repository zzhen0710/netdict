/// @file  common/guard/fd_guard.hpp
/// @brief RAII 封装文件描述符：析构自动 close；禁拷贝，允许移动。
/// 禁止拷贝：只被底层 fd 管理场景使用。

#pragma once

#include <unistd.h>   // close

/// @brief RAII 文件描述符守卫。
/// 语义：
///   - 构造时接管一个 fd（包括 -1，表示"空接管"）
///   - 析构时若 fd_ >= 0 自动 close
///   - 禁止拷贝（两个对象会 double close）
///   - 允许移动（所有权转移，源对象被置 -1，析构无操作）
class FdGuard {
public:
    /// @brief 接管一个 fd。
    // 传 -1 是合法的，表示"空接管"；析构时不做任何事。
    // 因此构造函数不校验 fd 合法性，交由调用者决定。
    explicit FdGuard(int fd) : fd_(fd) {}

    /// @brief 析构：若持有有效 fd，则 close。
    // 判断 fd_ >= 0 是为了处理"空接管"和"移动后源对象"两种情况。
    ~FdGuard() { if (fd_ >= 0) close(fd_); }

    // 禁止拷贝：两个 FdGuard 持同一 fd，析构会 close 两次（double close）。
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    /// @brief 移动构造：接管源对象的 fd，并把源对象置 -1。
    // 置 -1 的目的：使源对象析构时"无操作"，避免 double close。
    FdGuard(FdGuard&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    /// @brief 移动赋值：先释放自身旧 fd，再接管源对象的 fd，并把源对象置 -1。
    // 自我赋值检查（this != &other）：避免自己 close 自己后又接管自己，产生 UB。
    // 源对象置 -1 的目的：使源对象析构时"无操作"，避免 double close。
    FdGuard& operator=(FdGuard&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) close(fd_);   // 释放自身旧资源
            fd_ = other.fd_;            // 接管对方资源
            other.fd_ = -1;             // 源对象置空，析构无操作
        }
        return *this;
    }

    /// @brief 返回当前持有的 fd（不转移所有权）。
    int get() const { return fd_; }

    /// @brief 放弃所有权：返回 fd，并把自身置 -1，此后析构不再 close。
    // 用于把所有权交给外部（如注册进 epoll、传给别的资源管理器）。
    int release() noexcept {
        int fd = fd_;
        fd_ = -1;
        return fd;
    }

private:
    int fd_;
};