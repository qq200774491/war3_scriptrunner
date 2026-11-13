#include "Theme.h"

#include "Error.h"
#include "FileSystem.h"
#include "Json.h"
#include "Text.h"

#include <format>

namespace scriptroom::gui {

namespace {

constexpr Rgb gray(std::uint8_t Level) {
    return {Level, Level, Level};
}

/// 白色以 Alpha/255 的不透明度叠在底色上（Rust 版用半透明白表示图标栏选中态）。
constexpr Rgb lighten(Rgb Base, std::uint8_t Alpha) {
    const auto mix = [Alpha](std::uint8_t Channel) {
        return static_cast<std::uint8_t>(Channel + (255 - Channel) * Alpha / 255);
    };
    return {mix(Base.R), mix(Base.G), mix(Base.B)};
}

Palette light_palette() {
    Palette P;
    P.Canvas = {255, 255, 255};
    P.Panel = {246, 247, 249};
    P.Raised = {238, 240, 243};
    P.Border = {222, 225, 230};
    P.Divider = {232, 234, 238};
    P.Text = {45, 50, 58};
    P.Muted = {90, 98, 110};
    P.Accent = {55, 112, 215};
    P.Selected = {224, 234, 251};
    P.Rail = {233, 237, 244};
    P.OnAccent = {255, 255, 255};
    P.RailIcon = P.Muted;
    P.RailSelected = P.Selected;
    P.Danger = {196, 64, 64};
    return P;
}

Palette dark_palette(ThemeKind Kind) {
    const bool Blue = Kind == ThemeKind::Shadowworks;
    Palette P;
    P.Canvas = gray(32);
    P.Panel = gray(38);
    P.Raised = gray(45);
    P.Border = gray(57);
    P.Divider = gray(48);
    P.Text = {236, 238, 242};
    P.Muted = {185, 191, 201};
    P.Accent = Blue ? Rgb{74, 139, 240} : gray(216);
    P.Selected = Blue ? Rgb{35, 57, 86} : gray(60);
    P.Rail = Blue ? Rgb{23, 64, 124} : gray(30);
    P.OnAccent = Blue ? Rgb{255, 255, 255} : Rgb{0, 0, 0};
    P.RailIcon = {193, 209, 234};
    P.RailSelected = lighten(P.Rail, 28);
    P.Danger = {212, 100, 100};
    return P;
}

/// 这些控件共用同一种扁平外观：去掉渐变与浮雕，由调色板决定颜色。
constexpr std::array<const char *, 11> FlatWidgets = {
    "button",      "tool_button", "toggle_button", "radio_button", "checkbox", "text_field",
    "number_field", "dropdown",   "menu_item",     "list_item",    "box",
};

Json colors_of(const Palette &P) {
    Json Colors = {
        {"background", P.Canvas.hex()},
        {"foreground", P.Text.hex()},
        {"border", P.Border.hex()},
        {"primary.background", P.Accent.hex()},
        {"primary.foreground", P.OnAccent.hex()},
        {"secondary.background", P.Raised.hex()},
        {"secondary.foreground", P.Text.hex()},
        {"accent.background", P.Selected.hex()},
        {"accent.foreground", P.Text.hex()},
        {"popover.background", P.Raised.hex()},
        {"popover.foreground", P.Text.hex()},
        {"input.border", P.Border.hex()},
        {"text_field.background", P.Canvas.hex()},
        {"dropdown.background", P.Raised.hex()},
        {"box.background", P.Panel.hex()},
        {"list_item.background", P.Panel.hex()},
        {"panel.background", P.Panel.hex()},
        // 下拉框（menu）与菜单项。选中态沿用普通文字色，避免主题把“选中文字”设成反色后看不见。
        {"menu.background", P.Raised.hex()},
        {"menu.background.selected", P.Raised.hex()},
        {"menu.border", P.Border.hex()},
        {"menu.text", P.Text.hex()},
        {"menu.text.selected", P.Text.hex()},
        {"menu_item.text", P.Text.hex()},
        {"menu_item.text.selected", P.Text.hex()},
        // 勾选后的复选框标签用 text.selected 绘制，必须仍是普通文字色。
        {"checkbox.text.selected", P.Text.hex()},
        {"caret", P.Text.hex()},
        {"selection.background", P.Selected.hex()},
        {"scrollbar.fill", P.Border.hex()},
    };
    for (const char *Widget : FlatWidgets) {
        Colors[std::format("{}.gradient.enabled", Widget)] = false;
        Colors[std::format("{}.emboss.enabled", Widget)] = false;
    }
    return Colors;
}

} // namespace

std::string Rgb::hex() const {
    return std::format("#{:02x}{:02x}{:02x}", R, G, B);
}

Palette palette_of(ThemeKind Kind) {
    return Kind == ThemeKind::Light ? light_palette() : dark_palette(Kind);
}

const char *theme_label(ThemeKind Kind) {
    switch (Kind) {
    case ThemeKind::Monochrome:
        return "黑白灰";
    case ThemeKind::Light:
        return "浅色";
    case ThemeKind::Shadowworks:
        break;
    }
    return "深灰蓝";
}

const char *theme_key(ThemeKind Kind) {
    switch (Kind) {
    case ThemeKind::Monochrome:
        return "monochrome";
    case ThemeKind::Light:
        return "light";
    case ThemeKind::Shadowworks:
        break;
    }
    return "shadowworks";
}

std::optional<ThemeKind> theme_from_key(std::string_view Key) {
    for (const ThemeKind Kind : AllThemes) {
        if (Key == theme_key(Kind)) {
            return Kind;
        }
    }
    return std::nullopt;
}

std::string bui_theme_name(ThemeKind Kind) {
    return std::format("ScriptRoom {}", theme_key(Kind));
}

std::string bui_theme_json(ThemeKind Kind) {
    const Json Theme = {
        {"name", bui_theme_name(Kind)},
        {"mode", Kind == ThemeKind::Light ? "light" : "dark"},
        {"radius", 0.3},
        {"shadow", false},
        {"colors", colors_of(palette_of(Kind))},
    };
    const Json File = {{"name", "ScriptRoom"}, {"themes", Json::array({Theme})}};
    return dump_compact(File);
}

ThemeKind load_theme(const fs::path &Root) {
    try {
        const Json Body = parse_json(strip_bom(read_file(Root / L"界面设置.json")));
        const auto Found = Body.find("theme");
        if (Found != Body.end() && Found->is_string()) {
            return theme_from_key(Found->get<std::string>()).value_or(ThemeKind::Shadowworks);
        }
    } catch (const Error &) {
        // 文件缺失或损坏时回到默认主题，不阻止启动。
    }
    return ThemeKind::Shadowworks;
}

void save_theme(const fs::path &Root, ThemeKind Kind) {
    atomic_write(Root / L"界面设置.json", dump_pretty(Json{{"theme", theme_key(Kind)}}));
}

} // namespace scriptroom::gui
