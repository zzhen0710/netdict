/// @file db/dict_repo.hpp
/// @brief 字典仓储：对 dict 表的查询与维护。
/// 只依赖 db_guard 与 common；不涉及网络与并发。

#pragma once

#include "common/guard/db_guard.hpp"
#include "common/types.hpp"
#include <string>
#include <vector>

/// 字典仓储：管理 dict.db 的连接与词条操作。
/// 生命周期内持有 DbGuard，析构时自动关闭数据库。
class DictRepo {
public:
    /// 打开数据库；表与索引不存在则创建。
    /// @throws std::runtime_error 打开失败或建表失败时抛出。
    explicit DictRepo(const std::string& db_path);

    /// 从文本文件导入词库；表非空则跳过。
    /// 格式：每行 "<word> <mean>"，word 与 mean 之间允许有多空格。
    /// @return 导入成功或已存在（true）；打开文件失败（false）。
    bool initFromFile(const std::string& txt_path);

    /// 精确查询单词，返回该词的所有释义（含 rowid）。
    /// @param word  待查单词
    /// @param out   输出参数：全部释义（调用前会被清空）
    /// @return stat::Query::Ok（至少一条）或 stat::Query::NotFound
    stat::Query query(const std::string& word, std::vector<Meaning>& out);

    /// 词条总数。
    long long count();

private:
    DbGuard db_;   ///< 数据库连接（RAII：析构时关闭）
};