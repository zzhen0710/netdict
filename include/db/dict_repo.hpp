/// @file db/dict_repo.hpp
/// @brief 字典仓储：对 dict 表的查询与维护。
/// 只依赖 db_guard 与 common；不涉及网络与并发。
///
/// dict 表结构：dict(word, pos, mean)
///   word  单词
///   pos   词性（如 "adj." / "n."；可为空）
///   mean  释义文本
/// 一词多义 = 多行（同 word 多行，pos/mean 不同）。

#pragma once

#include "common/guard/db_guard.hpp"
#include "common/types.hpp"
#include <string>
#include <vector>

/// 一条词条：word + pos + mean（列表用）
struct DictEntry {
    std::string word;   ///< 单词
    std::string pos;    ///< 词性（可为空）
    std::string mean;   ///< 释义
};

/// 字典仓储：管理 dict.db 的连接与词条操作。
/// 生命周期内持有 DbGuard，析构时自动关闭数据库。
class DictRepo {
public:
    /// 打开数据库；表与索引不存在则创建。
    /// @throws std::runtime_error 打开失败或建表失败时抛出。
    explicit DictRepo(const std::string& db_path);

    /// 从文本文件导入词库；表非空则跳过。
    /// 格式：每行 "<word>\t<pos>\t<mean>"（TSV 三段）。
    ///   一词多义 = 多行（同 word 多行）。
    ///   pos 可为空（两个连续 Tab）。
    /// @return 导入成功或已存在（true）；打开文件失败（false）。
    bool initFromFile(const std::string& txt_path);

    /// 精确查询单词，返回该词的所有释义（带 pos）。
    /// @param word  待查单词
    /// @param out   输出参数：全部释义（调用前会被清空）
    /// @return status::Query::Ok（至少一条）/ NotFound / Err
    status::Query query(const std::string& word, std::vector<Meaning>& out);

    /// 词条总数。
    sqlite3_int64 count();

    // ---------- 管理接口（管理终端用） ----------

    /// 按模式列词。pattern 是 SQL LIKE 模式（'%' 通配）；
    /// 空 pattern 视为 "%"（列全部）。
    /// @param out  输出参数：匹配的词条（word + pos + mean）
    /// @return status::Admin::Ok（有无结果都算 Ok）/ Err
    status::Admin list(const std::string& pattern, std::vector<DictEntry>& out);

    /// 加词。
    /// @param mean  释义（pos + text）
    /// @return status::Admin::Ok / Err
    status::Admin add(const std::string& word, const Meaning& mean);

    /// 删词（该 word 的所有释义都删）。
    /// @return status::Admin::Ok / NotFound（无此词）/ Err
    status::Admin del(const std::string& word);

    /// 改词：把 word 的释义全替换为 mean（先删后加）。
    /// @return status::Admin::Ok / NotFound / Err
    status::Admin update(const std::string& word, const Meaning& mean);

    /// 重载词库：清空表 → 从文件重新导入。
    /// @return status::Admin::Ok / Err
    status::Admin reload(const std::string& txt_path);

private:
    DbGuard db_;   ///< 数据库连接（RAII：析构时关闭）
};