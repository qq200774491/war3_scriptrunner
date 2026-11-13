#include "Config.h"

#include "Error.h"
#include "FileSystem.h"
#include "Json.h"
#include "Text.h"
#include "Win32.h"

#include <array>
#include <cmath>
#include <format>

namespace scriptroom {

namespace {

constexpr std::array<const char *, 10> FieldNames = {
    "map", "local_entry", "cloud_scripts", "archives", "editor", "source_game", "game", "seconds",
    "startup_timeout", "clients"};

fs::path read_path_field(const Json &Body, const char *Name) {
    const auto Found = Body.find(Name);
    require(Found != Body.end(), std::format("配置缺少字段：{}", Name));
    require(Found->is_string(), std::format("配置字段 {} 应为字符串", Name));
    return path_from_utf8(Found->get<std::string>());
}

} // namespace

Config Config::load(const fs::path &Path) {
    std::string Text;
    try {
        Text = read_file(Path);
    } catch (const Error &Failure) {
        throw Error(std::format("读取配置 {}: {}", path_to_utf8(Path), Failure.what()));
    }
    require(is_valid_utf8(Text), "配置文件不是有效的 UTF-8");
    const Json Body = parse_json(strip_bom(Text));
    require(Body.is_object(), "配置应为 JSON 对象");
    for (const auto &Entry : Body.items()) {
        bool Known = false;
        for (const char *Name : FieldNames) {
            Known = Known || Entry.key() == Name;
        }
        require(Known, std::format("配置包含未知字段：{}", Entry.key()));
    }

    Config Result;
    Result.Map = read_path_field(Body, "map");
    Result.LocalEntry = read_path_field(Body, "local_entry");
    Result.CloudScripts = read_path_field(Body, "cloud_scripts");
    Result.Archives = read_path_field(Body, "archives");
    Result.Editor = read_path_field(Body, "editor");
    Result.SourceGame = read_path_field(Body, "source_game");
    Result.Game = read_path_field(Body, "game");

    const auto Seconds = Body.find("seconds");
    require(Seconds != Body.end(), "配置缺少字段：seconds");
    require(Seconds->is_number_unsigned(), "配置字段 seconds 应为非负整数");
    Result.Seconds = Seconds->get<std::uint64_t>();

    const auto Timeout = Body.find("startup_timeout");
    require(Timeout != Body.end(), "配置缺少字段：startup_timeout");
    require(Timeout->is_number(), "配置字段 startup_timeout 应为数字");
    Result.StartupTimeout = Timeout->get<double>();

    if (const auto Clients = Body.find("clients"); Clients != Body.end()) {
        require(Clients->is_number_unsigned(), "配置字段 clients（客户端个数）应为正整数");
        Result.Clients = Clients->get<std::size_t>();
    }

    const fs::path Parent = absolute_path(Path).parent_path();
    require(!Parent.empty(), "无配置目录");
    Result.resolve_paths(Parent);
    Result.validate_numbers();
    return Result;
}

void Config::resolve_paths(const fs::path &Parent) {
    for (fs::path *Value : {&Map, &LocalEntry, &CloudScripts, &Archives, &Editor, &SourceGame, &Game}) {
        require(!Value->empty(), "配置路径不能为空");
        if (Value->is_relative()) {
            *Value = Parent / *Value;
        }
    }
}

void Config::validate_numbers() const {
    require(Seconds > 0 && Seconds <= 31536000, "运行时长应在 1 秒至 1 年之间");
    require(std::isfinite(StartupTimeout) && StartupTimeout > 0.0 && StartupTimeout <= 3600.0,
            "启动超时应在 0 至 3600 秒之间");
    require(Clients >= MinClients && Clients <= MaxClients,
            std::format("客户端个数应在 {} 至 {} 之间", MinClients, MaxClients));
}

void Config::save(const fs::path &Path) const {
    validate_numbers();
    Json Body = {
        {"map", path_to_utf8(Map)},
        {"local_entry", path_to_utf8(LocalEntry)},
        {"cloud_scripts", path_to_utf8(CloudScripts)},
        {"archives", path_to_utf8(Archives)},
        {"editor", path_to_utf8(Editor)},
        {"source_game", path_to_utf8(SourceGame)},
        {"game", path_to_utf8(Game)},
        {"seconds", Seconds},
        {"startup_timeout", StartupTimeout},
        {"clients", Clients},
    };
    atomic_write(Path, dump_pretty(Body));
}

fs::path find_root() {
    std::wstring Buffer(32768, L'\0');
    const DWORD Length = GetModuleFileNameW(nullptr, Buffer.data(), static_cast<DWORD>(Buffer.size()));
    require(Length > 0 && Length < Buffer.size(), "无法取得程序路径");
    Buffer.resize(Length);
    fs::path Current = fs::path(Buffer).parent_path();
    while (!Current.empty()) {
        std::error_code Code;
        if (fs::is_regular_file(Current / L"scriptrunner.dll", Code) &&
            fs::is_regular_file(Current / L"map" / L"legacy_mls.lua", Code)) {
            return Current;
        }
        const fs::path Parent = Current.parent_path();
        if (Parent == Current) {
            break;
        }
        Current = Parent;
    }
    throw Error("找不到运行资源，请保留完整 ScriptRoom 文件夹");
}

fs::path default_config(const fs::path &Root) {
    return Root / path_from_utf8("本地测试配置.json");
}

} // namespace scriptroom
