/// @file db/usr_repo.cpp
/// @brief 用户仓储实现：注册 / 登录 / 登出 / 历史 / 收藏。

#include "db/usr_repo.hpp"

#include <stdexcept> // std::runtime_error

/// 构造：打开用户库，建表与索引。
/// @throws std::runtime_error 打开库或建表失败。
UsrRepo::UsrRepo(const std::string& db_path)
    : db_(db_path.c_str()) {

    // 三张表：usr（用户）/ history（历史）/ star（收藏）
    const char* sql =
        "create table if not exists usr ("
        "    name text primary key,"
        "    pwd text"
        ");"
        "create table if not exists history ("
        "    name text,"
        "    word text,"
        "    mean text,"
        "    time text"
        ");"
        "create index if not exists idx_history_name on history(name);"
        "create table if not exists star ("
        "    name text,"
        "    word text,"
        "    mean text," 
        "    time text,"
        "    primary key (name, word)"   // 同一用户不重复收藏
        ");";

    if (sqlite3_exec(db_.get(), sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        throw std::runtime_error(
            std::string("create table usr/history/star failed: ")
                        + sqlite3_errmsg(db_.get()));
    }
}

/// 注册新用户。
/// @return Ok（成功）/ Exists（用户名已存在）/ Err。
stat::UsrOp UsrRepo::reg(const std::string& name, const std::string& pwd) {
    StmtGuard stmt(db_.get(),
        "insert into usr (name, pwd) values (?, ?)");

    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 2, pwd.c_str(),  -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE)       return stat::UsrOp::Ok;
    if (rc == SQLITE_CONSTRAINT) return stat::UsrOp::Exists;

    return stat::UsrOp::Err;
}

/// 登录。
/// @return Ok / NotFound（用户不存在）/ WrongPwd / Err。
stat::UsrOp UsrRepo::login(const std::string& name, const std::string& pwd) {
    StmtGuard stmt(db_.get(), "select pwd from usr where name = ?");
    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_STATIC);

    // 查用户
    int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) return stat::UsrOp::NotFound;
    if (rc != SQLITE_ROW)  return stat::UsrOp::Err;

    // 密码校验
    const auto* db_pwd = sqlite3_column_text(stmt.get(), 0);
    if (!db_pwd || pwd != reinterpret_cast<const char*>(db_pwd))
        return stat::UsrOp::WrongPwd;

    return stat::UsrOp::Ok;
}

/// 追加一条历史记录。
bool UsrRepo::addHistory(const std::string& name, const HistoryEntry& entry) {
    StmtGuard stmt(db_.get(),
        "insert into history (name, word, mean, time) values (?, ?, ?, ?)");

    sqlite3_bind_text(stmt.get(), 1, name.c_str(),       -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 2, entry.word.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 3, entry.mean.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 4, entry.time.c_str(), -1, SQLITE_STATIC);

    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

/// 取最近 limit 条历史（按 rowid 倒序）。
bool UsrRepo::getHistory(const std::string& name, size_t limit,
                         std::vector<HistoryEntry>& out) {
    out.clear();

    // 用 rowid desc 代替 time desc：避免同秒多条时顺序不稳
    StmtGuard stmt(db_.get(),
        "select word, mean, time from history "
        "where name = ? order by rowid desc limit ?");

    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 2, static_cast<sqlite3_int64>(limit));

    // 逐行读结果；空结果也算成功
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        out.emplace_back();
        auto& e = out.back();

        const auto* w = sqlite3_column_text(stmt.get(), 0);
        const auto* m = sqlite3_column_text(stmt.get(), 1);
        const auto* t = sqlite3_column_text(stmt.get(), 2);

        e.word = w ? reinterpret_cast<const char*>(w) : "";
        e.mean = m ? reinterpret_cast<const char*>(m) : "";
        e.time = t ? reinterpret_cast<const char*>(t) : "";
    }

    return rc == SQLITE_DONE;
}

/// 收藏。
/// @return Ok / Starred（已收藏）/ Err。
stat::Query UsrRepo::star(const std::string& name, const StarEntry& entry) {
    StmtGuard stmt(db_.get(),
        "insert into star (name, word, mean, time) values (?, ?, ?, ?)");

    sqlite3_bind_text(stmt.get(), 1, name.c_str(),        -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 2, entry.word.c_str(),  -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 3, entry.mean.c_str(),  -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 4, entry.time.c_str(),  -1, SQLITE_STATIC);

    // 组合主键冲突 = 已收藏
    int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE)       return stat::Query::Ok;
    if (rc == SQLITE_CONSTRAINT) return stat::Query::Starred;

    return stat::Query::Err;
}

/// 取消收藏。
/// @return Ok / Unstarred（本来就没收藏）/ Err。
stat::Query UsrRepo::unstar(const std::string& name, const std::string& word) {
    StmtGuard stmt(db_.get(), "delete from star where name = ? and word = ?");
    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, word.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) return stat::Query::Err;

    // 影响行数为 0 = 本来就没收藏
    return sqlite3_changes(db_.get()) == 0
        ? stat::Query::Unstarred : stat::Query::Ok;
}

/// 取用户收藏（按 word 字母序，最多 limit 条）。
/// @return Ok / Err。
stat::Query UsrRepo::getStars(const std::string& name, size_t limit,
                              std::vector<StarEntry>& out) {
    out.clear();

    StmtGuard stmt(db_.get(),
        "select word, mean, time from star "
        "where name = ? order by word asc limit ?");

    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt.get(), 2, static_cast<sqlite3_int64>(limit));

    // 逐行读结果
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        const auto* w = sqlite3_column_text(stmt.get(), 0);
        const auto* m = sqlite3_column_text(stmt.get(), 1);
        const auto* t = sqlite3_column_text(stmt.get(), 2);
        out.push_back({
            w ? reinterpret_cast<const char*>(w) : "",
            m ? reinterpret_cast<const char*>(m) : "",
            t ? reinterpret_cast<const char*>(t) : ""
        });
    }

    return rc == SQLITE_DONE ? stat::Query::Ok : stat::Query::Err;
}