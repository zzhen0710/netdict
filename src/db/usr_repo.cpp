/// @file db/usr_repo.cpp
/// @brief 用户仓储实现：注册 / 登录 / 历史 / 收藏。
///
/// 表：
///   usr     (name primary key, pwd)
///   history (name, word, pos, mean, time)
///   star    (name, word, pos, mean, time, primary key(name,word,pos,mean))
/// 一词多义 = 多行（同 word 多行，pos/mean 不同）。

#include "db/usr_repo.hpp"

#include <stdexcept> // std::runtime_error

/// 构造：打开用户库，建表与索引。
/// @throws std::runtime_error 打开库或建表失败。
UsrRepo::UsrRepo(const std::string& db_path)
    : db_(db_path.c_str()) {

    // 三张表：
    //   usr     ：用户
    //   history ：历史（无主键，允许同词多行）
    //   star    ：收藏（主键 (name,word,pos,mean)：同词多义可存，
    //             重复收藏同释义才冲突）
    const char* sql =
        "create table if not exists usr ("
        "    name text primary key,"
        "    pwd text"
        ");"
        "create table if not exists history ("
        "    name text,"
        "    word text,"
        "    pos  text,"
        "    mean text,"
        "    time text"
        ");"
        "create index if not exists idx_history_name on history(name);"
        "create table if not exists star ("
        "    name text,"
        "    word text,"
        "    pos  text,"
        "    mean text,"
        "    time text,"
        "    primary key (name, word, pos, mean)"   // 同词多义可存
        ");";

    if (sqlite3_exec(db_.get(), sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        throw std::runtime_error(
            std::string("create table usr/history/star failed: ")
                        + sqlite3_errmsg(db_.get()));
    }
}

/// 注册新用户。
/// @return Ok（成功）/ Exists（用户名已存在）/ Err。
status::UsrOp UsrRepo::reg(const std::string& name, const std::string& pwd) {
    StmtGuard stmt(db_.get(),
        "insert into usr (name, pwd) values (?, ?)");

    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 2, pwd.c_str(),  -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE)       return status::UsrOp::Ok;
    if (rc == SQLITE_CONSTRAINT) return status::UsrOp::Exists;

    return status::UsrOp::Err;
}

/// 登录。
/// @return Ok / NotFound（用户不存在）/ WrongPwd / Err。
status::UsrOp UsrRepo::login(const std::string& name, const std::string& pwd) {
    StmtGuard stmt(db_.get(), "select pwd from usr where name = ?");
    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_STATIC);

    // 查用户
    int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) return status::UsrOp::NotFound;
    if (rc != SQLITE_ROW)  return status::UsrOp::Err;

    // 密码校验
    const auto* db_pwd = sqlite3_column_text(stmt.get(), 0);
    if (!db_pwd || pwd != reinterpret_cast<const char*>(db_pwd))
        return status::UsrOp::WrongPwd;

    return status::UsrOp::Ok;
}

/// 追加历史（一个词的所有释义，事务）。
/// @return 成功 true / 失败 false
bool UsrRepo::addHistory(const std::string& name,
                         const std::string& word,
                         const std::vector<Meaning>& means,
                         const std::string& time) {
    if (means.empty()) return true;   // 无释义，无需记

    // 事务：插全部释义
    if (sqlite3_exec(db_.get(), "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return false;
    }

    StmtGuard ins(db_.get(),
        "insert into history (name, word, pos, mean, time) values (?, ?, ?, ?, ?)");

    for (const auto& m : means) {
        // SQLITE_STATIC：name / word / m / time 活到函数结束
        sqlite3_bind_text(ins.get(), 1, name.c_str(),     -1, SQLITE_STATIC);
        sqlite3_bind_text(ins.get(), 2, word.c_str(),     -1, SQLITE_STATIC);
        sqlite3_bind_text(ins.get(), 3, m.pos.c_str(),    -1, SQLITE_STATIC);
        sqlite3_bind_text(ins.get(), 4, m.mean.c_str(),   -1, SQLITE_STATIC);
        sqlite3_bind_text(ins.get(), 5, time.c_str(),     -1, SQLITE_STATIC);

        if (sqlite3_step(ins.get()) != SQLITE_DONE) {
            sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }

        sqlite3_reset(ins.get());
        sqlite3_clear_bindings(ins.get());
    }

    if (sqlite3_exec(db_.get(), "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    return true;
}

/// 取最近 limit 条历史（按 rowid 倒序）。
bool UsrRepo::getHistory(const std::string& name, size_t limit,
                         std::vector<HistoryEntry>& out) {
    out.clear();

    // 用 rowid desc 代替 time desc：避免同秒多条时顺序不稳
    StmtGuard stmt(db_.get(),
        "select word, pos, mean, time from history "
        "where name = ? order by rowid desc limit ?");

    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 2, static_cast<sqlite3_int64>(limit));

    // 逐行读结果；空结果也算成功
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        const auto* w = sqlite3_column_text(stmt.get(), 0);
        const auto* p = sqlite3_column_text(stmt.get(), 1);
        const auto* m = sqlite3_column_text(stmt.get(), 2);
        const auto* t = sqlite3_column_text(stmt.get(), 3);

        out.push_back({
            w ? reinterpret_cast<const char*>(w) : "",
            p ? reinterpret_cast<const char*>(p) : "",
            m ? reinterpret_cast<const char*>(m) : "",
            t ? reinterpret_cast<const char*>(t) : ""
        });
    }

    return rc == SQLITE_DONE;
}

/// 收藏一个词（原子：插该 word 所有释义）。
/// 若该 word 已收藏（表里已有任何一行）→ Starred。
/// @return status::Query::Ok / Starred / Err
status::Query UsrRepo::star(const std::string& name,
                            const std::string& word,
                            const std::vector<Meaning>& means,
                            const std::string& time) {
    // 1. 查是否已收藏：该 word 有任何一行即已收藏（收藏是原子的，
    //    不会出现"部分释义在表里"）
    {
        StmtGuard chk(db_.get(),
            "select count(*) from star where name = ? and word = ?");
        sqlite3_bind_text(chk.get(), 1, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(chk.get(), 2, word.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(chk.get()) == SQLITE_ROW
                && sqlite3_column_int64(chk.get(), 0) > 0) {
            return status::Query::Starred;
        }
    }

    // 2. 事务：插该 word 的所有释义。
    //    事务保证原子：要么全插，要么全不插（不会"部分收藏"）。
    if (sqlite3_exec(db_.get(), "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return status::Query::Err;
    }

    StmtGuard ins(db_.get(),
        "insert into star (name, word, pos, mean, time) values (?, ?, ?, ?, ?)");

    for (const auto& m : means) {
        sqlite3_bind_text(ins.get(), 1, name.c_str(),   -1, SQLITE_STATIC);
        sqlite3_bind_text(ins.get(), 2, word.c_str(),   -1, SQLITE_STATIC);
        sqlite3_bind_text(ins.get(), 3, m.pos.c_str(),  -1, SQLITE_STATIC);
        sqlite3_bind_text(ins.get(), 4, m.mean.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(ins.get(), 5, time.c_str(),   -1, SQLITE_STATIC);

        if (sqlite3_step(ins.get()) != SQLITE_DONE) {
            sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            return status::Query::Err;
        }

        sqlite3_reset(ins.get());
        sqlite3_clear_bindings(ins.get());
    }

    if (sqlite3_exec(db_.get(), "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
        return status::Query::Err;
    }
    return status::Query::Ok;
}

/// 取消收藏（删该 word 的所有行，多义一起删）。
/// @return status::Query::Ok / Unstarred（本来就没收藏）/ Err
status::Query UsrRepo::unstar(const std::string& name, const std::string& word) {
    StmtGuard stmt(db_.get(), "delete from star where name = ? and word = ?");
    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, word.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) return status::Query::Err;

    // 影响行数为 0 = 本来就没收藏
    return sqlite3_changes(db_.get()) == 0
        ? status::Query::Unstarred : status::Query::Ok;
}

/// 取用户收藏（按 word 字母序，最多 limit 条）。
/// @return status::Query::Ok / Err
status::Query UsrRepo::getStars(const std::string& name, size_t limit,
                                std::vector<StarEntry>& out) {
    out.clear();

    StmtGuard stmt(db_.get(),
        "select word, pos, mean, time from star "
        "where name = ? order by word asc limit ?");

    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt.get(), 2, static_cast<sqlite3_int64>(limit));

    // 逐行读结果
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        const auto* w = sqlite3_column_text(stmt.get(), 0);
        const auto* p = sqlite3_column_text(stmt.get(), 1);
        const auto* m = sqlite3_column_text(stmt.get(), 2);
        const auto* t = sqlite3_column_text(stmt.get(), 3);

        out.push_back({
            w ? reinterpret_cast<const char*>(w) : "",
            p ? reinterpret_cast<const char*>(p) : "",
            m ? reinterpret_cast<const char*>(m) : "",
            t ? reinterpret_cast<const char*>(t) : ""
        });
    }

    return rc == SQLITE_DONE ? status::Query::Ok : status::Query::Err;
}