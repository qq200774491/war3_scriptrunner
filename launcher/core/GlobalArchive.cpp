#include "GlobalArchive.h"

#include "Crypto.h"
#include "Error.h"
#include "Text.h"

#include <algorithm>
#include <format>

namespace scriptroom {

namespace {

constexpr std::size_t MaxKeyBytes = 256;
constexpr std::size_t MaxValueBytes = 4096;
constexpr std::size_t MaxBatchBytes = 40 * 1024;
constexpr std::size_t MaxResultBytes = 64 * 1024;

void check_text(std::string_view Text, std::size_t Limit) {
    require(Text.size() <= Limit && Text.find('\0') == std::string_view::npos, "invalid global archive text");
}

/// 解析 1..100 个字段名组成的 JSON 数组。
std::vector<std::string> parse_fields(std::string_view Argument) {
    const Json Body = parse_json(Argument);
    require(Body.is_array(), "expected 1..100 field names");
    require(!Body.empty() && Body.size() <= 100, "expected 1..100 field names");
    std::vector<std::string> Fields;
    for (const Json &Item : Body) {
        require(Item.is_string(), "expected 1..100 field names");
        Fields.push_back(Item.get<std::string>());
        check_text(Fields.back(), MaxKeyBytes);
    }
    return Fields;
}

Json reply_body(std::string_view Operation, std::uint32_t Transaction, int Errnu, Json Data) {
    return Json{{"errnu", Errnu},
                {"op", std::string(Operation)},
                {"trans_id", Transaction},
                {"data", std::move(Data)}};
}

} // namespace

GlobalArchive::GlobalArchive(const std::filesystem::path &Directory)
    : Store(Directory / L"global.sqlite3") {
    Store.exec("PRAGMA synchronous=FULL; CREATE TABLE IF NOT EXISTS fields (key TEXT NOT NULL, field "
               "TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY(key,field));");
}

GlobalReply GlobalArchive::request(std::uint32_t Transaction, std::string_view Operation,
                                   std::string_view Key, std::string_view Argument) {
    Json Data;
    try {
        Data = checked_request(Operation, Key, Argument);
    } catch (const StorageError &) {
        throw;
    } catch (const Error &Failure) {
        Json Body = reply_body(Operation, Transaction, -1, Json::object());
        Body["local_error"] = Failure.what();
        return {"_msga_reply", std::move(Body)};
    }
    if (Operation == "MsBroadcast") {
        return {"_msbroadcast", std::move(Data)};
    }
    return {"_msga_reply", reply_body(Operation, Transaction, 0, std::move(Data))};
}

Json GlobalArchive::checked_request(std::string_view Operation, std::string_view Key,
                                    std::string_view Argument) {
    check_text(Key, MaxKeyBytes);
    require(!Key.empty(), "empty global archive key");
    const std::string KeyText(Key);
    if (Operation == "MsBroadcast") {
        check_text(Argument, MaxValueBytes);
        return Json{{"key", KeyText}, {"value", std::string(Argument)}};
    }
    if (Operation == "MsGALockAcquire" || Operation == "MsGALockRelease") {
        return lease(Operation, KeyText, Argument);
    }
    if (Operation == "MsGAHmSet" || Operation == "MsGAHmGet" || Operation == "MsGAHGetAll" ||
        Operation == "MsGAHDel") {
        return hash(Operation, KeyText, Argument);
    }
    throw Error("unsupported local global archive operation");
}

bool GlobalArchive::lock_active(const std::string &Key) const {
    const auto Found = Locks.find(Key);
    return Found != Locks.end() && Found->second > Clock::now();
}

Json GlobalArchive::lease(std::string_view Operation, const std::string &Key, std::string_view Argument) {
    {
        Statement Query(Store, "SELECT EXISTS(SELECT 1 FROM fields WHERE key=?)");
        Query.bind_text(1, Key);
        Query.step();
        require(Query.column_int(0) == 0, "key is a hash, not a lock");
    }
    if (Operation == "MsGALockRelease") {
        Locks.erase(Key);
        return Json{{"ok", true}};
    }
    const std::optional<std::int64_t> Requested = parse_i64(trim(Argument));
    require(Requested.has_value(), "lock ttl is not an integer");
    const std::int64_t Ttl = std::clamp<std::int64_t>(*Requested, 1000, 300000);
    const Clock::time_point Now = Clock::now();
    if (lock_active(Key)) {
        return Json{{"ok", false}, {"token", ""}, {"ttl_ms", Ttl}};
    }
    Locks[Key] = Now + std::chrono::milliseconds(Ttl);
    return Json{{"ok", true}, {"token", random_hex(16)}, {"ttl_ms", Ttl}};
}

Json GlobalArchive::hash(std::string_view Operation, const std::string &Key, std::string_view Argument) {
    require(!lock_active(Key), "key is a lock, not a hash");
    Transaction Scope(Store);
    Json Data;
    if (Operation == "MsGAHmSet") {
        require(Argument.size() <= MaxBatchBytes, "hash batch exceeds 40 KiB");
        const Json Input = parse_json(Argument);
        require(Input.is_object() && !Input.empty(), "expected nonempty hash object");
        for (const auto &Entry : Input.items()) {
            require(Entry.value().is_string(), "invalid global archive text");
            const std::string Value = Entry.value().get<std::string>();
            check_text(Entry.key(), MaxKeyBytes);
            check_text(Value, MaxValueBytes);
            Statement Write(Store,
                            "INSERT INTO fields VALUES (?,?,?) ON CONFLICT(key,field) DO UPDATE SET "
                            "value=excluded.value");
            Write.bind_text(1, Key);
            Write.bind_text(2, Entry.key());
            Write.bind_text(3, Value);
            Write.step();
        }
        Data = Json{{"value", Input.size()}};
    } else if (Operation == "MsGAHmGet") {
        Json Values = Json::object();
        for (const std::string &Field : parse_fields(Argument)) {
            Statement Read(Store, "SELECT value FROM fields WHERE key=? AND field=?");
            Read.bind_text(1, Key);
            Read.bind_text(2, Field);
            if (Read.step()) {
                Values[Field] = Read.column_text(0);
            }
        }
        Data = Json{{"value", std::move(Values)}};
    } else if (Operation == "MsGAHGetAll") {
        Json Values = Json::object();
        Statement Read(Store, "SELECT field,value FROM fields WHERE key=? ORDER BY field");
        Read.bind_text(1, Key);
        while (Read.step()) {
            Values[Read.column_text(0)] = Read.column_text(1);
        }
        Data = Json{{"value", std::move(Values)}};
    } else {
        std::int64_t Count = 0;
        for (const std::string &Field : parse_fields(Argument)) {
            Statement Remove(Store, "DELETE FROM fields WHERE key=? AND field=?");
            Remove.bind_text(1, Key);
            Remove.bind_text(2, Field);
            Remove.step();
            Count += Remove.changes();
        }
        Data = Json{{"value", Count}};
    }
    require(dump_compact(Data).size() <= MaxResultBytes, "global archive result exceeds 64 KiB");
    Scope.commit();
    return Data;
}

} // namespace scriptroom
