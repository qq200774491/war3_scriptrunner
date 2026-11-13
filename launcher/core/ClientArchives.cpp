#include "ClientArchives.h"

#include "Error.h"
#include "FileSystem.h"
#include "Ini.h"
#include "Json.h"
#include "Text.h"

#include <format>
#include <map>
#include <set>
#include <span>
#include <system_error>
#include <vector>

namespace scriptroom {

namespace {

/// 解析 SSV-<玩家>-<名称> 形式的键，返回玩家号；格式不符返回 nullopt。
std::optional<std::uint64_t> key_owner(std::string_view Key) {
    const std::size_t First = Key.find('-');
    if (First == std::string_view::npos || !equals_ignore_case(Key.substr(0, First), "SSV")) {
        return std::nullopt;
    }
    const std::size_t Second = Key.find('-', First + 1);
    if (Second == std::string_view::npos) {
        return std::nullopt;
    }
    const std::optional<std::uint64_t> Player = parse_u64(Key.substr(First + 1, Second - First - 1));
    if (!Player || Second + 1 >= Key.size()) {
        return std::nullopt;
    }
    return Player;
}

/// 存档里最多出现的玩家号（一局魔兽 12 人）。
constexpr std::uint64_t PlayerLimit = 12;

std::span<const std::uint8_t> as_bytes(const std::string &Text) {
    return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(Text.data()), Text.size());
}

struct Layout {
    int Version = 0;
    std::set<std::size_t> Players; ///< 记过存档的玩家
};

/// 布局标记：缺失为空。v1 只有两人；v2 记下用过存档的玩家号，必须互不相同且小于 12。
std::optional<Layout> read_layout(const fs::path &Marker) {
    std::error_code Code;
    if (!fs::exists(Marker, Code)) {
        return std::nullopt;
    }
    const Json Body = parse_json(read_file(Marker));
    const char *Unknown = "未知客户端存档布局，已保留原文件";
    if (Body == Json{{"version", 1}, {"players", {0, 1}}}) {
        return Layout{1, {0, 1}};
    }
    require(Body.is_object() && Body.size() == 2 && Body.value("version", 0) == 2, Unknown);
    const auto Listed = Body.find("players");
    require(Listed != Body.end() && Listed->is_array() && !Listed->empty(), Unknown);
    Layout Result{2, {}};
    for (const Json &Player : *Listed) {
        require(Player.is_number_unsigned() && Player.get<std::uint64_t>() < PlayerLimit, Unknown);
        require(Result.Players.insert(Player.get<std::size_t>()).second, Unknown);
    }
    return Result;
}

struct PlayerDocument {
    Ini Document;
    std::string OriginalBytes;
};

} // namespace

std::vector<fs::path> prepare_client_archives(const fs::path &Folder, std::size_t Players,
                                              const std::optional<fs::path> &Legacy) {
    require(Players >= 1 && Players <= PlayerLimit, "客户端个数超出范围");
    std::error_code Code;
    fs::create_directories(Folder, Code);
    require(!Code, std::format("创建存档目录失败：{}", Code.message()));

    const fs::path Marker = Folder / L"layout.json";
    const std::optional<Layout> Known = read_layout(Marker);
    const bool Migrating = Known && Known->Version == 1;
    // 记过的玩家这一轮不开也要整理：他的权威值照样同步给队友，“文件缺失就拒绝”也照常生效。
    std::set<std::size_t> Handled = Known ? Known->Players : std::set<std::size_t>();
    for (std::size_t Player = 0; Player < Players; ++Player) {
        Handled.insert(Player);
    }
    const std::vector<std::size_t> Roster(Handled.begin(), Handled.end());
    const auto path_of = [&Folder](std::size_t Player) { return Folder / std::format(L"player-{}.ini", Player); };

    std::vector<FileLock> Locks;
    for (const std::size_t Player : Roster) {
        Locks.push_back(exclusive_file(path_of(Player).native() + L".lock"));
    }

    const fs::path Backup = Folder / L"legacy-dz_w3_plugin.ini.bak";
    std::string Seed;
    if (!Known && fs::exists(Backup, Code)) {
        Seed = read_file(Backup);
    } else if (!Known && Legacy && fs::is_regular_file(*Legacy, Code)) {
        Seed = read_file(*Legacy);
        Ini::parse(as_bytes(Seed));
        atomic_write(Backup, Seed);
    } else {
        Seed = "[DzAPI]\r\n";
    }

    // 所有权威槽位读完后才写快照；绝不能用队友的旧副本复活已删除的存档。
    std::vector<PlayerDocument> Documents;
    std::map<std::string, std::string> Owned;
    for (const std::size_t Player : Roster) {
        const bool Listed = Known && Known->Players.contains(Player);
        const fs::path Source =
            Migrating && Listed ? Folder / std::format(L"player-{}", Player) / L"dz_w3_plugin.ini" : path_of(Player);
        std::string Bytes;
        if (Listed || fs::exists(Source, Code)) {
            try {
                Bytes = read_file(Source);
            } catch (const Error &Failure) {
                throw Error(std::format("读取玩家 {} 存档 {}: {}", Player, path_to_utf8(Source), Failure.what()));
            }
        } else {
            Bytes = Seed;
        }
        Ini Document = Ini::parse(as_bytes(Bytes));
        for (const auto &Field : Document.section_mut("DzAPI")) {
            if (key_owner(Field.first) == Player) {
                Owned[Field.first] = Field.second;
            }
        }
        Documents.push_back({std::move(Document), std::move(Bytes)});
    }

    const auto owned_by_roster = [&Handled](std::string_view Key) {
        const std::optional<std::uint64_t> Owner = key_owner(Key);
        return Owner && Handled.contains(static_cast<std::size_t>(*Owner));
    };
    for (std::size_t Index = 0; Index < Roster.size(); ++Index) {
        const std::size_t Player = Roster[Index];
        PlayerDocument &Entry = Documents[Index];
        if (Migrating && Known->Players.contains(Player)) {
            const fs::path Old = Folder / L"backups" / std::format(L"v1-player-{}.ini", Player);
            if (!fs::exists(Old, Code)) {
                atomic_write(Old, Entry.OriginalBytes);
            }
        }
        Ini::Fields &Fields = Entry.Document.section_mut("DzAPI");
        for (auto Iterator = Fields.begin(); Iterator != Fields.end();) {
            Iterator = owned_by_roster(Iterator->first) ? Fields.erase(Iterator) : std::next(Iterator);
        }
        for (const auto &Field : Owned) {
            Fields[Field.first] = Field.second;
        }
        atomic_write(path_of(Player), Entry.Document.utf16());
    }

    atomic_write(Marker, dump_compact(Json{{"version", 2}, {"players", Roster}}));
    std::vector<fs::path> Paths;
    for (std::size_t Player = 0; Player < Players; ++Player) {
        Paths.push_back(path_of(Player));
    }
    return Paths;
}

} // namespace scriptroom
