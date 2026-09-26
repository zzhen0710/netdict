/// @file db/usr_repo.hpp
/// @brief 用户仓储：注册 / 登录 / 登出 / 历史 / 收藏。

#pragma once

#include "common/guard/db_guard.hpp"
#include "common/types.hpp"
#include <string>
#include <vector>

/// 一条历史记录
struct HistoryEntry {
    std::string word;
    std::string mean;
    std::string time;
};

class UsrRepo {
public:
    /// 打开用户库，建表与索引
    explicit UsrRepo(const std::string& db_path);

    // ---------- 用户 ----------

    /// 注册；用户名已存在返回 Exists
    stat::UsrOp reg(const std::string& name, const std::string& pwd);

    /// 登录；用户不存在返回 NotFound，密码错返回 WrongPwd
    /// 已在别处登录返回 Online
    stat::UsrOp login(const std::string& name, const std::string& pwd);

    /// 登出
    stat::UsrOp logout(const std::string& name);

    // ---------- 历史 ----------

    /// 追加一条历史记录
    bool addHistory(const std::string& name, const HistoryEntry& entry);

    /// 取最近 limit 条历史（按时间倒序）
    bool getHistory(const std::string& name, size_t limit,
                    std::vector<HistoryEntry>& out);

    // ---------- 收藏 ----------

    /// 收藏；已收藏返回 Exists
    stat::Query star(const std::string& name, const std::string& word);

    /// 取消收藏；未收藏返回 NotFound
    stat::Query unstar(const std::string& name, const std::string& word);

    /// 查该用户收藏的词，最多 limit 条；返回 Ok / Err
    stat::Query getStars(const std::string& name, size_t limit, 
                         std::vector<std::string>& out);

private:
    DbGuard db_;
};