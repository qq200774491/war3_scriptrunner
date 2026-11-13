#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace scriptroom {

/// SQLite 连接。所有失败都抛出 StorageError。
class Database {
public:
    explicit Database(const std::filesystem::path &Path);
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    /// 执行不含参数的多条语句。
    void exec(const char *Sql);

    sqlite3 *handle() const { return Handle; }

private:
    sqlite3 *Handle = nullptr;
};

/// 预编译语句；文本参数按 UTF-8 绑定，序号从 1 开始。
class Statement {
public:
    Statement(Database &Owner, const char *Sql);
    ~Statement();
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    void bind_text(int Index, std::string_view Value);

    /// 推进到下一行；有行返回 true，结束返回 false。
    bool step();

    std::string column_text(int Index) const;
    std::int64_t column_int(int Index) const;

    /// 上一次 DML 影响的行数。
    int changes() const;

private:
    Database &Owner;
    sqlite3_stmt *Handle = nullptr;
};

/// 事务；未显式 commit 就析构会回滚。
class Transaction {
public:
    explicit Transaction(Database &Owner);
    ~Transaction();
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;

    void commit();

private:
    Database &Owner;
    bool Open = true;
};

} // namespace scriptroom
