/// @file db/dict_repo.cpp
/// @brief 字典仓储实现：打开/建表、导入词库、查询与计数。

#include "db/dict_repo.hpp"

#include <fstream>   // std::ifstream, std::getline
#include <stdexcept> // std::runtime_error

/// 构造：打开数据库，建表与索引。
/// @throws std::runtime_error 打开库或建表失败。
DictRepo::DictRepo(const std::string& db_path)
    : db_(db_path.c_str()) {

    // 建表 + 给 word 建索引（加速等值查询）
    const char* sql =
        "create table if not exists dict ("
        "   word text,"
        "   mean text"
        ");"
        "create index if not exists idx_dict_word on dict(word);";

    if (sqlite3_exec(db_.get(), sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        throw std::runtime_error(
            std::string("create table dict failed: ") + sqlite3_errmsg(db_.get()));
    }
}

/// 从文本文件导入词条；表非空则跳过。
/// 格式：每行 "<word> <mean>"，word 与 mean 之间允许有多空格。
bool DictRepo::initFromFile(const std::string& txt_path) {
    if (count() > 0) return true;

    std::ifstream file(txt_path);
    if (!file) return false;

    // 预编译 insert，循环内重用
    StmtGuard stmt(db_.get(), "insert into dict (word, mean) values (?, ?)");
    std::string line;

    // 事务包住全部插入，大幅加速
    if (sqlite3_exec(db_.get(), "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;

    while (std::getline(file, line)) {
        if (line.empty()) continue;   // 跳过空行

        // 定位 word：跳过行首空格，到第一个空格
        auto word_start = line.find_first_not_of(' ');
        if (word_start == std::string::npos) continue;
        auto word_end = line.find(' ', word_start);
        if (word_end == std::string::npos) continue;

        // 定位 mean：跳过中间多空格，到行尾非空格
        auto mean_start = line.find_first_not_of(' ', word_end + 1);
        if (mean_start == std::string::npos) continue;
        auto mean_end = line.find_last_not_of(' ');

        std::string word = line.substr(word_start, word_end - word_start);
        std::string mean = line.substr(mean_start, mean_end - mean_start + 1);

        // 绑定 + 执行
        sqlite3_bind_text(stmt.get(), 1, word.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.get(), 2, mean.c_str(), -1, SQLITE_TRANSIENT);
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

/// 精确查询：按 word 查所有释义（含 rowid）。
/// @return Ok（至少一条）/ NotFound（无结果）/ Err（读取异常）。
stat::Query DictRepo::query(const std::string& word, std::vector<Meaning>& out) {
    out.clear();

    // SQLITE_STATIC：word 是函数参数，活到函数结束，无需复制
    StmtGuard stmt(db_.get(), "select rowid, mean from dict where word = ?");
    sqlite3_bind_text(stmt.get(), 1, word.c_str(), -1, SQLITE_STATIC);

    // 一个 word 可能多条释义：逐行读，直到 SQLITE_DONE
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        out.emplace_back();          // 先占位再回填，省一次移动
        auto& m = out.back();

        m.rowid = sqlite3_column_int64(stmt.get(), 0);          // 列 0：rowid
        const auto* txt = sqlite3_column_text(stmt.get(), 1);   // 列 1：mean（可能 NULL）
        m.text = txt ? reinterpret_cast<const char*>(txt) : ""; // 判空防 UB
    }

    if (rc != SQLITE_DONE) return stat::Query::Err;
    return out.empty() ? stat::Query::NotFound : stat::Query::Ok;
}

/// 词条总数。
long long DictRepo::count() {
    StmtGuard stmt(db_.get(), "select count(*) from dict");

    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        return sqlite3_column_int64(stmt.get(), 0);
    }
    return 0;   // 理论上到不了
}