#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace scriptroom::gui {

namespace fs = std::filesystem;

/// 三套配色，名称与 Rust 版一致；Shadowworks 为默认。
enum class ThemeKind { Shadowworks, Monochrome, Light };

inline constexpr std::array<ThemeKind, 3> AllThemes = {ThemeKind::Shadowworks, ThemeKind::Monochrome,
                                                       ThemeKind::Light};

struct Rgb {
    std::uint8_t R = 0;
    std::uint8_t G = 0;
    std::uint8_t B = 0;

    bool operator==(const Rgb &) const = default;

    /// "#rrggbb"，BUI 主题文件使用的颜色写法。
    std::string hex() const;
};

/// 界面使用的全部颜色。数值取自 Rust 版 theme.rs，另含 BUI 版需要的派生色。
struct Palette {
    Rgb Canvas;
    Rgb Panel;
    Rgb Raised;
    Rgb Border;
    Rgb Divider;
    Rgb Text;
    Rgb Muted;
    Rgb Accent;
    Rgb Selected;
    Rgb Rail;
    Rgb OnAccent;     ///< 主操作按钮上的文字：黑白灰主题用黑字，其余用白字
    Rgb RailIcon;     ///< 左侧图标栏的图标色
    Rgb RailSelected; ///< 图标栏当前页的底色
    Rgb Danger;       ///< 日志来源列中的“错误”
};

Palette palette_of(ThemeKind Kind);

/// 主题下拉框中的显示名：深灰蓝 / 黑白灰 / 浅色。
const char *theme_label(ThemeKind Kind);

/// 界面设置.json 中的取值：shadowworks / monochrome / light。
const char *theme_key(ThemeKind Kind);
std::optional<ThemeKind> theme_from_key(std::string_view Key);

/// BUI 里注册该主题时使用的名称。
std::string bui_theme_name(ThemeKind Kind);

/// 生成 BUI 主题文件（JSON 文本）：浅色基于 "light"，其余基于 "dark"。
std::string bui_theme_json(ThemeKind Kind);

/// 读取 Root/界面设置.json；缺失或内容无效时返回默认主题。
ThemeKind load_theme(const fs::path &Root);

/// 以 {"theme": "..."} 原子写入 Root/界面设置.json；失败时抛出 Error。
void save_theme(const fs::path &Root, ThemeKind Kind);

} // namespace scriptroom::gui
