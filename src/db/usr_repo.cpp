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
        "    time text,"
        "    batch integer"       // 批次号：同次 addHistory 的所有行共享
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

// ---------- 用户 ----------

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

// usr_repo.cpp
bool UsrRepo::exists(const std::string& name) {
    StmtGuard stmt(db_.get(), "select 1 from usr where name = ? limit 1");
    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_STATIC);

    return sqlite3_step(stmt.get()) == SQLITE_ROW;
}

// ---------- 历史 ----------

/// 追加历史（一个词的所有释义，事务）。
/// 每次调用分配一个新 batch（该 name 下 max(batch)+1），同次所有行共享，
/// 用于把"一次查询"的多行归到同一词头。
/// @return 成功 true / 失败 false
bool UsrRepo::addHistory(const std::string& name,
                         const std::string& word,
                         const std::vector<Meaning>& means,
                         const std::string& time) {
    if (means.empty()) return true;   // 无释义，无需记

    // 1. 算本次 batch = max(batch)+1（该 name 下）
    // batch 用于把"同一次查询"插入的多行释义归为一组
    long long batch = 0;
    {
        // coalesce(max(batch), 0)：无记录时 max 为 NULL，用 0 兜底
        // +1 得到本次新 batch，保证每个用户下递增
        StmtGuard b(db_.get(),
            "select coalesce(max(batch), 0) + 1 from history where name = ?");
        sqlite3_bind_text(b.get(), 1, name.c_str(), -1, SQLITE_STATIC);

        // 聚合查询必有且仅有一行，SQLITE_ROW 即取到值
        if (sqlite3_step(b.get()) == SQLITE_ROW) {
            batch = sqlite3_column_int64(b.get(), 0);   // 第 0 列即算出的 batch
        }
        // 离开作用域：StmtGuard 自动 finalize
    }

    // 2. 事务：插全部释义
    // 显式 BEGIN，保证下面多条 insert 要么全成功、要么全回滚
    if (sqlite3_exec(db_.get(), "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return false;
    }

    // 预编译 insert，循环里复用（StmtGuard 负责析构时 finalize）
    // 列顺序：name, word, pos, mean, time, batch
    StmtGuard ins(db_.get(),
        "insert into history (name, word, pos, mean, time, batch) "
        "values (?, ?, ?, ?, ?, ?)");

    for (const auto& m : means) {
        // SQLITE_STATIC：name / word / m.pos / m.mean / time 活到函数结束
        // 绑定 6 个占位符：1=name 2=word 3=pos 4=mean 5=time 6=batch
        sqlite3_bind_text(ins.get(), 1, name.c_str(),   -1, SQLITE_STATIC);
        sqlite3_bind_text(ins.get(), 2, word.c_str(),   -1, SQLITE_STATIC);
        sqlite3_bind_text(ins.get(), 3, m.pos.c_str(),  -1, SQLITE_STATIC);
        sqlite3_bind_text(ins.get(), 4, m.mean.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(ins.get(), 5, time.c_str(),   -1, SQLITE_STATIC);
        // batch 是整数，用 bind_int64（不是 text）
        sqlite3_bind_int64(ins.get(), 6, batch);

        // 执行本条 insert；非 SQLITE_DONE 视为失败
        if (sqlite3_step(ins.get()) != SQLITE_DONE) {
            // 任一条失败：整体回滚，保证原子性
            sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }
        // 复位语句、清空绑定，供下一条释义复用
        sqlite3_reset(ins.get());
        sqlite3_clear_bindings(ins.get());
    }

    // 全部插入成功：提交事务
    if (sqlite3_exec(db_.get(), "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
        // 提交失败：回滚，避免留下半提交状态
        sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    return true;
}

/// 取最近 limit 次查询（一次查询 = 一个 batch；同词多次 = 多 batch）。
/// 组间按 batch 倒序（最新查询在前）；组内按 rowid 升序（插入顺序）。
bool UsrRepo::getHistory(const std::string& name, size_t limit,
                         std::vector<HistoryEntry>& out) {
    out.clear();   // 清空输出

    // 内层：按 (word, batch) 分组，取前 limit 个 batch（batch desc = 最新在前）；
    // 外层：把这些 (word, batch) 的行取出来，
    //       组间 batch desc（新查询在前），组内 rowid asc（插入顺序）。
    // 目的：limit 限制"最近几次查询"，一次查询一个 batch；
    //       同词多次查询 → 多个 batch，各自成组。
    //
    // 例（limit = 2，同词每次查询 2 条释义）：
    //   内层选出最近 2 个 batch：  (cat,4)、(apple,3)
    //   外层取这两个 batch 的所有行，按 batch desc, rowid asc：
    //     cat    n. 猫      batch 4
    //     apple  n. 苹果    batch 3
    //     apple  n. 苹果树  batch 3
    StmtGuard stmt(db_.get(),
        "select word, pos, mean, time, batch from history "
        "where name = ? and (word, batch) in ("
        "    select word, batch from history "
        "    where name = ? "
        "    group by word, batch "
        "    order by batch desc "
        "    limit ?"
        ") "
        "order by batch desc, rowid asc");

    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);   // 外层 name
    sqlite3_bind_text(stmt.get(), 2, name.c_str(), -1, SQLITE_TRANSIENT);   // 子查询 name
    sqlite3_bind_int64(stmt.get(), 3, static_cast<sqlite3_int64>(limit));   // batch 数上限

    // 逐行读结果；空结果也算成功
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        const auto* w = sqlite3_column_text(stmt.get(), 0);   // 列 0：word
        const auto* p = sqlite3_column_text(stmt.get(), 1);   // 列 1：pos
        const auto* m = sqlite3_column_text(stmt.get(), 2);   // 列 2：mean
        const auto* t = sqlite3_column_text(stmt.get(), 3);   // 列 3：time

        // 判空防 UB；列 4 是 batch（int64）
        out.push_back({
            w ? reinterpret_cast<const char*>(w) : "",
            p ? reinterpret_cast<const char*>(p) : "",
            m ? reinterpret_cast<const char*>(m) : "",
            t ? reinterpret_cast<const char*>(t) : "",
            sqlite3_column_int64(stmt.get(), 4)          // batch
        });
    }

    // DONE 才算正常读完（空结果也是 DONE）
    return rc == SQLITE_DONE;
}

// ---------- 收藏 ----------

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
    out.clear();   // 清空输出

    // 内层：取该用户收藏的前 limit 个不同 word（字母序）；
    // 外层：把这些 word 的所有释义取出来（按 word 排序）。
    // 目的：limit 限制的是"词数"，不是"行数"。
    //
    // 例（limit = 2，apple/book 各 2 条释义）：
    //   内层选出前 2 个 word：  apple、book
    //   外层取这两个 word 的所有行，按 word asc：
    //     apple  n. 苹果
    //     apple  n. 苹果树
    //     book   n. 书
    //     book   n. 书册
    StmtGuard stmt(db_.get(),
        "select word, pos, mean, time from star "
        "where name = ? and word in ("
        "    select distinct word from star "
        "    where name = ? "
        "    order by word asc "
        "    limit ?"
        ") "
        "order by word asc");

    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_STATIC);   // 外层 name
    sqlite3_bind_text(stmt.get(), 2, name.c_str(), -1, SQLITE_STATIC);   // 内层 name
    sqlite3_bind_int64(stmt.get(), 3, static_cast<sqlite3_int64>(limit)); // 词数上限

    // 逐行读结果
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        const auto* w = sqlite3_column_text(stmt.get(), 0);   // 列 0：word
        const auto* p = sqlite3_column_text(stmt.get(), 1);   // 列 1：pos
        const auto* m = sqlite3_column_text(stmt.get(), 2);   // 列 2：mean
        const auto* t = sqlite3_column_text(stmt.get(), 3);   // 列 3：time

        // 判空防 UB；直接 emplace
        out.push_back({
            w ? reinterpret_cast<const char*>(w) : "",
            p ? reinterpret_cast<const char*>(p) : "",
            m ? reinterpret_cast<const char*>(m) : "",
            t ? reinterpret_cast<const char*>(t) : ""
        });
    }

    // DONE 才算正常读完；否则 Err
    return rc == SQLITE_DONE ? status::Query::Ok : status::Query::Err;
}