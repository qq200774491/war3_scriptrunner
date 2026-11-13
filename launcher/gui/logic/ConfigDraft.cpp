#include "ConfigDraft.h"

#include "Error.h"
#include "FileSystem.h"
#include "Text.h"

#include <format>

namespace scriptroom::gui {

namespace {

/// 草稿字段与 Config 成员的对应表，from_config / to_config 共用，保证顺序一致。
struct FieldBinding {
    PathField Field;
    fs::path Config::*Member;
};

constexpr std::array<FieldBinding, PathFieldCount> Bindings = {{
    {PathField::Map, &Config::Map},
    {PathField::LocalEntry, &Config::LocalEntry},
    {PathField::CloudScripts, &Config::CloudScripts},
    {PathField::Archives, &Config::Archives},
    {PathField::Editor, &Config::Editor},
    {PathField::SourceGame, &Config::SourceGame},
    {PathField::Game, &Config::Game},
}};

} // namespace

ConfigDraft ConfigDraft::from_config(const Config &Settings) {
    ConfigDraft Draft;
    for (const FieldBinding &Binding : Bindings) {
        Draft.path(Binding.Field) = path_to_utf8(Settings.*Binding.Member);
    }
    Draft.Seconds = std::to_string(Settings.Seconds);
    Draft.Timeout = std::format("{}", Settings.StartupTimeout);
    Draft.Clients = Settings.Clients;
    return Draft;
}

Config ConfigDraft::to_config(const fs::path &ConfigPath) const {
    Config Result;
    for (const FieldBinding &Binding : Bindings) {
        const std::string_view Text = trim(path(Binding.Field));
        require(!Text.empty(), "请补全地图、脚本、存档和运行环境路径");
        Result.*Binding.Member = path_from_utf8(Text);
    }
    const auto ParsedSeconds = parse_u64(trim(Seconds));
    require(ParsedSeconds.has_value(), "服务时长应为非负整数（秒）");
    Result.Seconds = *ParsedSeconds;
    const auto ParsedTimeout = parse_double(trim(Timeout));
    require(ParsedTimeout.has_value(), "启动超时应为数字（秒）");
    Result.StartupTimeout = *ParsedTimeout;
    Result.Clients = Clients;
    Result.validate_numbers();

    const fs::path Parent = absolute_path(ConfigPath).parent_path();
    require(!Parent.empty(), "无配置目录");
    Result.resolve_paths(Parent);
    return Result;
}

} // namespace scriptroom::gui
