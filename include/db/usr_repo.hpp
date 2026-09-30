/// @file db/usr_repo.hpp
/// @brief 用户仓储：注册 / 登录 / 登出 / 历史 / 收藏。
///
/// 表结构：
///   usr     (name, pwd)
///   history (name, word, pos, mean, time)
///   star    (name, word, pos, mean, time)
/// 一词多义 = 多行（同 word 多行，pos/mean 不同）。
/// star 主键 (name, word, pos, mean)：同词多义可存，重复收藏同释义才冲突。

#pragma once

#include "common/guard/db_guard.hpp"
#include "common/types.hpp"
#include <string>
#include <vector>

/// 一条历史记录
struct HistoryEntry {
    std::string   word;    ///< 单词
    std::string   pos;     ///< 词性（可为空）
    std::string   mean;    ///< 释义
    std::string   time;    ///< 时间
    sqlite3_int64 batch;   ///< 批次号（同一次查询的所有行共享；用于分组）
};

/// 一条收藏记录
struct StarEntry {
    std::string word;   ///< 单词
    std::string pos;    ///< 词性（可为空）
    std::string mean;   ///< 释义
    std::string time;   ///< 时间
};

class UsrRepo {
public:
    /// 打开用户库，建表与索引
    explicit UsrRepo(const std::string& db_path);

    // ---------- 用户 ----------

    /// 注册；用户名已存在返回 Exists
    status::UsrOp reg(const std::string& name, const std::string& pwd);

    /// 登录；用户不存在返回 NotFound，密码错返回 WrongPwd
    status::UsrOp login(const std::string& name, const std::string& pwd);

    // ---------- 历史 ----------

    /// 追加历史（一个词的所有释义，事务）。
    /// @param name   用户名
    /// @param word   单词
    /// @param means  该 word 的全部释义（query 的结果）
    /// @param time   时间（调用方算好传入）
    /// @return 成功 true / 失败 false
    bool addHistory(const std::string& name,
                    const std::string& word,
                    const std::vector<Meaning>& means,
                    const std::string& time);

    /// 取最近 limit 条历史（按 rowid 倒序）
    bool getHistory(const std::string& name, size_t limit,
                    std::vector<HistoryEntry>& out);

    // ---------- 收藏 ----------

    /// 收藏一个词（原子：插该 word 所有释义）。
    /// 若该 word 已收藏（表里已有任何一行）→ 返回 Starred。
    /// @return status::Query::Ok / Starred / Err
    status::Query star(const std::string& name,
                       const std::string& word,
                       const std::vector<Meaning>& means,
                       const std::string& time);

    /// 取消收藏（删该 word 的所有行，多义一起删）。
    /// @return status::Query::Ok / Unstarred（本来就没收藏）/ Err
    status::Query unstar(const std::string& name, const std::string& word);

    /// 查该用户收藏的词（字母序），最多 limit 条，查得 word + pos + mean；
    /// @return status::Query::Ok / Err
    status::Query getStars(const std::string& name, size_t limit,
                           std::vector<StarEntry>& out);

private:
    DbGuard db_;
};