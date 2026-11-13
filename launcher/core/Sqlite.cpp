#include "Sqlite.h"

#include "Error.h"
#include "Text.h"

#include "sqlite3/sqlite3.h"

#include <format>

namespace scriptroom {

namespace {

[[noreturn]] void throw_sqlite(sqlite3 *Handle, const char *Action) {
    throw StorageError(std::format("{}：{}", Action, sqlite3_errmsg(Handle)));
}

} // namespace

Database::Database(const std::filesystem::path &Path) {
    const std::string Utf8 = path_to_utf8(Path);
    const int Code = sqlite3_open_v2(Utf8.c_str(), &Handle,
                                     SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                     nullptr);
    if (Code != SQLITE_OK) {
        const std::string Reason = Handle != nullptr ? sqlite3_errmsg(Handle) : sqlite3_errstr(Code);
        sqlite3_close(Handle);
        Handle = nullptr;
        throw StorageError(std::format("打开全局存档 {} 失败：{}", Utf8, Reason));
    }
}

Database::~Database() {
    sqlite3_close(Handle);
}

void Database::exec(const char *Sql) {
    char *Message = nullptr;
    if (sqlite3_exec(Handle, Sql, nullptr, nullptr, &Message) != SQLITE_OK) {
        const std::string Reason = Message != nullptr ? Message : "未知错误";
        sqlite3_free(Message);
        throw StorageError("执行 SQL 失败：" + Reason);
    }
}

Statement::Statement(Database &OwnerDatabase, const char *Sql) : Owner(OwnerDatabase) {
    if (sqlite3_prepare_v2(Owner.handle(), Sql, -1, &Handle, nullptr) != SQLITE_OK) {
        throw_sqlite(Owner.handle(), "准备 SQL 失败");
    }
}

Statement::~Statement() {
    sqlite3_finalize(Handle);
}

void Statement::bind_text(int Index, std::string_view Value) {
    if (sqlite3_bind_text(Handle, Index, Value.data(), static_cast<int>(Value.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        throw_sqlite(Owner.handle(), "绑定参数失败");
    }
}

bool Statement::step() {
    const int Code = sqlite3_step(Handle);
    if (Code == SQLITE_ROW) {
        return true;
    }
    if (Code == SQLITE_DONE) {
        return false;
    }
    throw_sqlite(Owner.handle(), "执行语句失败");
}

std::string Statement::column_text(int Index) const {
    const unsigned char *Text = sqlite3_column_text(Handle, Index);
    if (Text == nullptr) {
        return {};
    }
    return std::string(reinterpret_cast<const char *>(Text),
                       static_cast<std::size_t>(sqlite3_column_bytes(Handle, Index)));
}

std::int64_t Statement::column_int(int Index) const {
    return sqlite3_column_int64(Handle, Index);
}

int Statement::changes() const {
    return sqlite3_changes(Owner.handle());
}

Transaction::Transaction(Database &OwnerDatabase) : Owner(OwnerDatabase) {
    Owner.exec("BEGIN");
}

Transaction::~Transaction() {
    if (Open) {
        sqlite3_exec(Owner.handle(), "ROLLBACK", nullptr, nullptr, nullptr);
    }
}

void Transaction::commit() {
    Owner.exec("COMMIT");
    Open = false;
}

} // namespace scriptroom
