#include "ClientArchives.h"

#include "Error.h"
#include "FileSystem.h"
#include "Ini.h"
#include "Json.h"
#include "Text.h"

#include <format>
#include <map>
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

bool owned_by_known_player(std::string_view Key) {
    const std::optional<std::uint64_t> Owner = key_owner(Key);
    return Owner && (*Owner == 0 || *Owner == 1);
}

std::span<const std::uint8_t> as_bytes(const std::string &Text) {
    return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(Text.data()), Text.size());
}

/// 布局标记：缺失为 null，其余只接受已知的 v1/v2。
Json read_layout(const fs::path &Marker) {
    std::error_code Code;
    if (!fs::exists(Marker, Code)) {
        return Json(nullptr);
    }
    const Json Layout = parse_json(read_file(Marker));
    const Json V1 = {{"version", 1}, {"players", {0, 1}}};
    const Json V2 = {{"version", 2}, {"players", {0, 1}}};
    require(Layout == V1 || Layout == V2, "未知客户端存档布局，已保留原文件");
    return Layout;
}

struct PlayerDocument {
    Ini Document;
    std::string OriginalBytes;
};

} // namespace

std::array<fs::path, 2> prepare_client_archives(const fs::path &Folder,
                                                const std::optional<fs::path> &Legacy) {
    std::error_code Code;
    fs::create_directories(Folder, Code);
    require(!Code, std::format("创建存档目录失败：{}", Code.message()));
    const std::array<fs::path, 2> Paths = {Folder / L"player-0.ini", Folder / L"player-1.ini"};

    std::vector<FileLock> Locks;
    for (const fs::path &Path : Paths) {
        Locks.push_back(exclusive_file(Path.native() + L".lock"));
    }

    const fs::path Marker = Folder / L"layout.json";
    const Json Layout = read_layout(Marker);
    const bool Migrating = !Layout.is_null() && Layout.at("version") == 1;

    const fs::path Backup = Folder / L"legacy-dz_w3_plugin.ini.bak";
    std::string Seed;
    if (Layout.is_null() && fs::exists(Backup, Code)) {
        Seed = read_file(Backup);
    } else if (Layout.is_null() && Legacy && fs::is_regular_file(*Legacy, Code)) {
        Seed = read_file(*Legacy);
        Ini::parse(as_bytes(Seed));
        atomic_write(Backup, Seed);
    } else {
        Seed = "[DzAPI]\r\n";
    }

    // 所有权威槽位读完后才写快照；绝不能用队友的旧副本复活已删除的存档。
    std::vector<PlayerDocument> Documents;
    std::map<std::string, std::string> Owned;
    for (std::size_t Player = 0; Player < Paths.size(); ++Player) {
        const fs::path Source =
            Migrating ? Folder / std::format(L"player-{}", Player) / L"dz_w3_plugin.ini" : Paths[Player];
        std::string Bytes;
        if (!Layout.is_null() || fs::exists(Source, Code)) {
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

    for (std::size_t Player = 0; Player < Documents.size(); ++Player) {
        PlayerDocument &Entry = Documents[Player];
        if (Migrating) {
            const fs::path Old = Folder / L"backups" / std::format(L"v1-player-{}.ini", Player);
            if (!fs::exists(Old, Code)) {
                atomic_write(Old, Entry.OriginalBytes);
            }
        }
        Ini::Fields &Fields = Entry.Document.section_mut("DzAPI");
        for (auto Iterator = Fields.begin(); Iterator != Fields.end();) {
            Iterator = owned_by_known_player(Iterator->first) ? Fields.erase(Iterator) : std::next(Iterator);
        }
        for (const auto &Field : Owned) {
            Fields[Field.first] = Field.second;
        }
        atomic_write(Paths[Player], Entry.Document.utf16());
    }

    atomic_write(Marker, dump_compact(Json{{"version", 2}, {"players", {0, 1}}}));
    return Paths;
}

} // namespace scriptroom
