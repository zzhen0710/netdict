/// @file db/dict_repo.cpp
/// @brief 字典仓储实现：打开/建表、导入词库、查询、管理。
///
/// 表结构：dict(word, pos, mean)
///   一词多义 = 多行。
/// 导入文件：TSV 三段 "<word>\t<pos>\t<mean>"。

#include "db/dict_repo.hpp"

#include <fstream>   // std::ifstream, std::getline
#include <stdexcept> // std::runtime_error

/// 构造：打开数据库，建表与索引。
/// @throws std::runtime_error 打开库或建表失败。
DictRepo::DictRepo(const std::string& db_path)
    : db_(db_path.c_str()) {

    // 建表 + 给 word 建索引（加速等值查询）
    // pos 可为空（TEXT 默认 NULL，但我们总显式插字符串）。
    const char* sql =
        "create table if not exists dict ("
        "   word text,"
        "   pos  text,"
        "   mean text"
        ");"
        "create index if not exists idx_dict_word on dict(word);";

    if (sqlite3_exec(db_.get(), sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        throw std::runtime_error(
            std::string("create table dict failed: ") + sqlite3_errmsg(db_.get()));
    }
}

/// 从文本文件导入词条；表非空则跳过。
/// 格式：每行 "<word>\t<pos>\t<mean>"（TSV 三段）。
///   word：非空，不含 Tab。
///   pos ：可为空（两个连续 Tab，如 "able\t\t释义"）。
///   mean：非空，到行尾（可含空格）。
bool DictRepo::initFromFile(const std::string& txt_path) {
    if (count() > 0) return true;

    std::ifstream file(txt_path);
    if (!file) return false;

    // 预编译 insert，循环内重用
    StmtGuard stmt(db_.get(),
        "insert into dict (word, pos, mean) values (?, ?, ?)");

    std::string line;

    // 事务包住全部插入，大幅加速
    if (sqlite3_exec(db_.get(), "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;

    while (std::getline(file, line)) {
        // 跳空行
        if (line.empty()) continue;

        // 切三段：第一个 Tab 前是 word；第二个 Tab 前是 pos；剩下是 mean
        auto tab1 = line.find('\t');
        if (tab1 == std::string::npos) continue;   // 无 Tab，坏行，跳过

        auto tab2 = line.find('\t', tab1 + 1);
        if (tab2 == std::string::npos) continue;   // 只有一个 Tab，坏行，跳过

        std::string word = line.substr(0, tab1);
        std::string pos  = line.substr(tab1 + 1, tab2 - tab1 - 1);
        std::string mean = line.substr(tab2 + 1);

        // word 空则跳过
        if (word.empty()) continue;

        // 绑定 + 执行
        sqlite3_bind_text(stmt.get(), 1, word.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.get(), 2, pos.c_str(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.get(), 3, mean.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
            sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }

        // 重置 stmt，准备下一条
        sqlite3_reset(stmt.get());
        sqlite3_clear_bindings(stmt.get());
    }

    if (sqlite3_exec(db_.get(), "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    return true;
}

/// 精确查询：按 word 查所有释义（带 pos）。
/// @return Ok（至少一条）/ NotFound（无结果）/ Err（读取异常）。
status::Query DictRepo::query(const std::string& word, std::vector<Meaning>& out) {
    out.clear();

    // SQLITE_STATIC：word 是函数参数，活到函数结束，无需复制
    StmtGuard stmt(db_.get(), "select pos, mean from dict where word = ?");
    sqlite3_bind_text(stmt.get(), 1, word.c_str(), -1, SQLITE_STATIC);

    // 一个 word 可能多条释义：逐行读，直到 SQLITE_DONE
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        out.emplace_back();
        auto& m = out.back();

        const auto* pos  = sqlite3_column_text(stmt.get(), 0);   // 列 0：pos（可能 NULL）
        const auto* mean = sqlite3_column_text(stmt.get(), 1);   // 列 1：mean（可能 NULL）

        m.pos  = pos  ? reinterpret_cast<const char*>(pos)  : "";
        m.mean = mean ? reinterpret_cast<const char*>(mean) : "";
    }

    if (rc != SQLITE_DONE) return status::Query::Err;
    return out.empty() ? status::Query::NotFound : status::Query::Ok;
}

/// 词条总数。
sqlite3_int64 DictRepo::count() {
    StmtGuard stmt(db_.get(), "select count(*) from dict");

    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        return sqlite3_column_int64(stmt.get(), 0);
    }
    return 0;   // 理论上到不了
}

// ---------- 管理接口 ----------

/// 按模式列词（SQL LIKE）。pattern 里 '%' 通配；空 pattern 视为 "%"。
/// @return status::Admin::Ok（有无结果都算 Ok）/ Err
status::Admin DictRepo::list(const std::string& pattern, std::vector<DictEntry>& out) {
    out.clear();

    // 空 pattern 列全部
    std::string pat = pattern.empty() ? "%" : pattern;

    StmtGuard stmt(db_.get(),
        "select word, pos, mean from dict where word like ? order by word");

    // SQLITE_TRANSIENT：pat 是局部变量，让 sqlite 自己复制一份，安全
    sqlite3_bind_text(stmt.get(), 1, pat.c_str(), -1, SQLITE_TRANSIENT);

    // 逐行读；一个 word 可能多条释义
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        const auto* w = sqlite3_column_text(stmt.get(), 0);
        const auto* p = sqlite3_column_text(stmt.get(), 1);
        const auto* m = sqlite3_column_text(stmt.get(), 2);

        out.push_back({
            w ? reinterpret_cast<const char*>(w) : "",
            p ? reinterpret_cast<const char*>(p) : "",
            m ? reinterpret_cast<const char*>(m) : ""
        });
    }

    return rc == SQLITE_DONE ? status::Admin::Ok : status::Admin::Err;
}

/// 加词。
/// @return status::Admin::Ok / Err
status::Admin DictRepo::add(const std::string& word, const Meaning& mean) {
    StmtGuard stmt(db_.get(), "insert into dict (word, pos, mean) values (?, ?, ?)");

    // SQLITE_STATIC：word / mean 活到函数结束，无需复制
    sqlite3_bind_text(stmt.get(), 1, word.c_str(),      -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 2, mean.pos.c_str(),  -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 3, mean.mean.c_str(), -1, SQLITE_STATIC);

    return sqlite3_step(stmt.get()) == SQLITE_DONE
        ? status::Admin::Ok : status::Admin::Err;
}

/// 删词（该 word 的所有释义都删）。
/// @return status::Admin::Ok / NotFound / Err
status::Admin DictRepo::del(const std::string& word) {
    StmtGuard stmt(db_.get(), "delete from dict where word = ?");
    sqlite3_bind_text(stmt.get(), 1, word.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) return status::Admin::Err;

    // 影响行数为 0 = 没这个词
    return sqlite3_changes(db_.get()) == 0
        ? status::Admin::NotFound : status::Admin::Ok;
}

/// 改词：把 word 的释义全替换为 mean（先删后加）。
/// @return status::Admin::Ok / NotFound / Err
status::Admin DictRepo::update(const std::string& word, const Meaning& mean) {
    // 先删（同时判断词是否存在）
    auto d = del(word);
    if (d != status::Admin::Ok) return d;   // NotFound / Err

    // 再加新释义
    return add(word, mean);
}

/// 重载词库：清空表 → 从文件重新导入。
/// @return status::Admin::Ok / Err
status::Admin DictRepo::reload(const std::string& txt_path) {
    // 清空表
    if (sqlite3_exec(db_.get(), "delete from dict", nullptr, nullptr, nullptr)
            != SQLITE_OK) {
        return status::Admin::Err;
    }

    // 重新导入（此时表空，initFromFile 会导入）
    return initFromFile(txt_path) ? status::Admin::Ok : status::Admin::Err;
}