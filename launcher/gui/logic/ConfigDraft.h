#pragma once

#include "Config.h"

#include <array>
#include <cstddef>
#include <string>

namespace scriptroom::gui {

/// 配置页里可编辑的路径字段，数值即 ConfigDraft::Paths 的下标。
enum class PathField : std::size_t {
    Map,
    LocalEntry,
    CloudScripts,
    Archives,
    Editor,
    SourceGame,
    Game,
};

inline constexpr std::size_t PathFieldCount = 7;

/// 配置页的编辑草稿：所有字段都是用户输入的原始文本，转换与校验只发生在 to_config。
struct ConfigDraft {
    std::array<std::string, PathFieldCount> Paths; ///< UTF-8
    std::string Seconds;
    std::string Timeout;

    std::string &path(PathField Field) { return Paths[static_cast<std::size_t>(Field)]; }
    const std::string &path(PathField Field) const { return Paths[static_cast<std::size_t>(Field)]; }

    static ConfigDraft from_config(const Config &Settings);

    /// 转成可运行的配置：路径相对于 ConfigPath 所在目录，数值经 validate_numbers 校验。
    /// 任一路径为空、数值无法解析或越界时抛出 Error（信息可直接展示）。
    Config to_config(const fs::path &ConfigPath) const;

    bool operator==(const ConfigDraft &) const = default;
};

} // namespace scriptroom::gui
