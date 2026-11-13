#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace scriptroom::gui {

namespace fs = std::filesystem;

enum class ThemeKind { Blue, Sky, Teal, Violet, Amber, Slate, White };

inline constexpr std::array<ThemeKind, 7> AllThemes = {ThemeKind::Blue,  ThemeKind::Sky,   ThemeKind::Teal,
                                                       ThemeKind::Violet, ThemeKind::Amber, ThemeKind::Slate,
                                                       ThemeKind::White};

struct Rgb {
    std::uint8_t R = 0;
    std::uint8_t G = 0;
    std::uint8_t B = 0;

    bool operator==(const Rgb &) const = default;

    /// "#rrggbb"，BUI 主题文件使用的颜色写法。
    std::string hex() const;
};

Rgb blend(Rgb Base, Rgb Over, float Alpha);

struct Palette {
    Rgb Canvas;
    Rgb Panel;
    Rgb Side;    ///< 左栏底色，只比主区域深一点，让人看出是两块
    Rgb Divider; ///< 区域之间的 1px 淡线，只比底色亮一点
    Rgb Card;
    Rgb Edge;
    Rgb Popup;
    Rgb Text;
    Rgb Text2;
    Rgb Text3;
    Rgb Text4;
    Rgb Icon;
    Rgb Danger;
    Rgb Success;

    Rgb Accent;
    Rgb AccentDeep;
    Rgb AccentSolid;
    Rgb AccentHover;
    Rgb AccentPressed;
    Rgb AccentBright;
    Rgb AccentOn;

    Rgb ControlHover;
    Rgb ControlPressed;
    Rgb EdgeHover;
    Rgb Overlay; ///< 半透明悬停层的颜色：深色主题叠白，白色主题叠黑
    Rgb FieldEditing;
    Rgb CheckFill;
    Rgb CheckEdge;
    float TileAlpha = 0.18f;
    bool Light = false;

    /// 图表里各玩家的颜色：第一个是主题色，其余色相互相分开；玩家多于 6 个时循环使用。
    std::array<Rgb, 6> Series;
};

Palette palette_of(ThemeKind Kind);

Rgb theme_swatch(ThemeKind Kind);

const char *theme_label(ThemeKind Kind);

const char *theme_key(ThemeKind Kind);

std::optional<ThemeKind> theme_from_key(std::string_view Key);

/// BUI 里注册该主题时使用的名称。
std::string bui_theme_name(ThemeKind Kind);

std::string bui_theme_json(ThemeKind Kind);

/// 读取 Root/界面设置.json；缺失或内容无效时返回默认主题。
ThemeKind load_theme(const fs::path &Root);

/// 以 {"theme": "..."} 原子写入 Root/界面设置.json；失败时抛出 Error。
void save_theme(const fs::path &Root, ThemeKind Kind);

} // namespace scriptroom::gui
