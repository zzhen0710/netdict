/// @file  common/guard/db_guard.hpp
/// @brief RAII 封装：自动管理 sqlite3* 与 sqlite3_stmt* 的生命周期。
/// 禁止拷贝；只被 db/ 与 ser/ 使用。

#pragma once

#include <sqlite3.h>    // sqlite3, sqlite3_stmt, sqlite3_close, sqlite3_finalize, sqlite3_errmsg
#include <stdexcept>    // std::runtime_error
#include <string>       // std::string

/// @brief RAII 数据库连接守卫。
/// 语义：
///   - 构造时打开数据库，接管 sqlite3* 句柄
///   - 析构时若 pdb_ != nullptr 自动 sqlite3_close
///   - 禁止拷贝（两个对象会 double close）
///   - 允许移动（所有权转移，源对象被置 nullptr，析构无操作）
class DbGuard {
public:
    /// @brief 打开或创建数据库，接管 sqlite3* 句柄。
    // 失败时抛 std::runtime_error。
    // 失败时必须 close 的原因（官方文档指导）：
    //   据 SQLite 文档：无论 sqlite3_open_v2 是否成功，
    //   与其关联的资源都应在不再需要时通过 sqlite3_close 释放。
    //   即"打开失败"不等于"句柄未分配"——句柄通常已分配，只是文件打不开。
    explicit DbGuard(const char* path) {
        if (path == nullptr) {
            throw std::runtime_error("DbGuard: null path");
        }
        if (sqlite3_open_v2(path, &pdb_,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
            std::string msg = sqlite3_errmsg(pdb_);   // 先取错误消息
            sqlite3_close(pdb_);                       // 依据官方文档：失败时句柄可能已分配
            throw std::runtime_error(msg);
        }
    }

    /// @brief 析构：若持有有效句柄，则 close。
    // 判断 pdb_ != nullptr 是为了处理"移动后源对象"的情况。
    ~DbGuard() { if (pdb_ != nullptr) sqlite3_close(pdb_); }

    // 禁止拷贝：两个 DbGuard 持同一句柄，析构会 close 两次（double close）。
    DbGuard(const DbGuard&) = delete;
    DbGuard& operator=(const DbGuard&) = delete;

    /// @brief 移动构造：接管源对象的句柄，并把源对象置 nullptr。
    // 置 nullptr 的目的：使源对象析构时"无操作"，避免 double close。
    DbGuard(DbGuard&& other) noexcept : pdb_(other.pdb_) {
        other.pdb_ = nullptr;
    }

    /// @brief 移动赋值：先释放自身旧句柄，再接管源对象的句柄，并把源对象置 nullptr。
    // 自我赋值检查（this != &other）：避免自己 close 自己后又接管自己，产生 UB。
    // 源对象置 nullptr 的目的：使源对象析构时"无操作"，避免 double close。
    DbGuard& operator=(DbGuard&& other) noexcept {
        if (this != &other) {
            if (pdb_ != nullptr) sqlite3_close(pdb_);    // 释放自身旧资源
            pdb_ = other.pdb_;                           // 接管对方资源
            other.pdb_ = nullptr;                        // 源对象置空，析构无操作
        }
        return *this;
    }

    /// @brief 返回当前持有的句柄（不转移所有权）。
    [[nodiscard]] sqlite3* get() const { return pdb_; }

private:
    sqlite3* pdb_ = nullptr;
};

/// @brief RAII 预编译语句守卫。
/// 语义：
///   - 构造时预编译 SQL，接管 sqlite3_stmt* 句柄
///   - 析构时若 pstmt_ != nullptr 自动 sqlite3_finalize
///   - 禁止拷贝（两个对象会 double finalize）
///   - 允许移动（所有权转移，源对象被置 nullptr，析构无操作）
class StmtGuard {
public:
    /// @brief 预编译 SQL，接管 sqlite3_stmt* 句柄。
    // 失败时抛 std::runtime_error。
    // 失败时不需要 finalize 的原因（官方文档指导）：
    //   据 SQLite 文档：sqlite3_prepare_v2 出错时 *ppStmt 会被置为 NULL。
    //   即"prepare 失败"保证 stmt 未分配，无需清理。
    explicit StmtGuard(sqlite3* pdb, const char* sql) {
        if (pdb == nullptr) {
            throw std::runtime_error("StmtGuard: null db");
        }
        if (sqlite3_prepare_v2(pdb, sql, -1, &pstmt_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string(sqlite3_errmsg(pdb)));
        }
    }

    /// @brief 析构：若持有有效句柄，则 finalize。
    // 判断 pstmt_ != nullptr 是为了处理"移动后源对象"的情况。
    ~StmtGuard() { if (pstmt_ != nullptr) sqlite3_finalize(pstmt_); }

    // 禁止拷贝：两个 StmtGuard 持同一句柄，析构会 finalize 两次（double finalize）。
    StmtGuard(const StmtGuard&) = delete;
    StmtGuard& operator=(const StmtGuard&) = delete;

    /// @brief 移动构造：接管源对象的句柄，并把源对象置 nullptr。
    // 置 nullptr 的目的：使源对象析构时"无操作"，避免 double finalize。
    StmtGuard(StmtGuard&& other) noexcept : pstmt_(other.pstmt_) {
        other.pstmt_ = nullptr;
    }

    /// @brief 移动赋值：先释放自身旧句柄，再接管源对象的句柄，并把源对象置 nullptr。
    // 自我赋值检查（this != &other）：避免自己 finalize 自己后又接管自己，产生 UB。
    // 源对象置 nullptr 的目的：使源对象析构时"无操作"，避免 double finalize。
    StmtGuard& operator=(StmtGuard&& other) noexcept {
        if (this != &other) {
            if (pstmt_ != nullptr) sqlite3_finalize(pstmt_);    // 释放自身旧资源
            pstmt_ = other.pstmt_;                              // 接管对方资源
            other.pstmt_ = nullptr;                             // 源对象置空，析构无操作
        }
        return *this;
    }

    /// @brief 返回当前持有的句柄（不转移所有权）。
    [[nodiscard]] sqlite3_stmt* get() const { return pstmt_; }

private:
    sqlite3_stmt* pstmt_ = nullptr;
};