#include "Theme.h"

#include "Error.h"
#include "FileSystem.h"
#include "Json.h"
#include "Text.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace scriptroom::gui {

namespace {

constexpr Rgb gray(std::uint8_t Level) {
    return {Level, Level, Level};
}

constexpr Rgb White = {255, 255, 255};

struct ThemeSpec {
    ThemeKind Kind;
    const char *Key;
    const char *Label;
    bool Light;
    Rgb Base;
    Rgb Deep;
    Rgb Solid;
    Rgb SolidHover;
    Rgb SolidPressed;
    Rgb Bright;
    Rgb On;
};

constexpr std::array<ThemeSpec, 7> Themes = {{
    {ThemeKind::Blue, "blue", "蓝", false, {0x0B, 0x3F, 0x96}, {0x08, 0x30, 0x73}, {0x01, 0x43, 0xB1}, {0x1A, 0x55, 0xC4},
     {0x21, 0x69, 0xE6}, {0x14, 0x70, 0xE1}, {0x8F, 0xB6, 0xF0}},
    {ThemeKind::Sky, "sky", "天蓝", false, {0x00, 0x78, 0xD4}, {0x00, 0x4E, 0x8C}, {0x00, 0x78, 0xD4}, {0x1A, 0x86, 0xD8},
     {0x00, 0x6C, 0xBE}, {0x2B, 0x8F, 0xE6}, {0x9C, 0xCB, 0xF5}},
    {ThemeKind::Teal, "teal", "青", false, {0x0B, 0x5F, 0x55}, {0x07, 0x42, 0x3B}, {0x0E, 0x7A, 0x6C}, {0x12, 0x91, 0x83},
     {0x1A, 0xB3, 0xA0}, {0x17, 0xA0, 0x8D}, {0x7B, 0xD8, 0xC8}},
    {ThemeKind::Violet, "violet", "紫", false, {0x3D, 0x2A, 0x85}, {0x2B, 0x1C, 0x60}, {0x51, 0x38, 0xAC},
     {0x61, 0x46, 0xC4}, {0x7A, 0x5C, 0xE8}, {0x7B, 0x5B, 0xE0}, {0xBC, 0xA8, 0xF5}},
    {ThemeKind::Amber, "amber", "琥珀", false, {0x7A, 0x4A, 0x08}, {0x5A, 0x35, 0x05}, {0x9C, 0x61, 0x0B},
     {0xB3, 0x74, 0x12}, {0xCE, 0x8A, 0x1C}, {0xD1, 0x8A, 0x17}, {0xF0, 0xC0, 0x71}},
    {ThemeKind::Slate, "slate", "石板", false, {0x2A, 0x38, 0x44}, {0x1D, 0x28, 0x31}, {0x37, 0x49, 0x5A},
     {0x45, 0x60, 0x77}, {0x58, 0x77, 0x95}, {0x5E, 0x7F, 0x9C}, {0xA6, 0xC2, 0xD8}},
    {ThemeKind::White, "white", "白色", true, {0xE8, 0xED, 0xF6}, {0x2A, 0x6B, 0xD8}, {0x14, 0x6E, 0xE0},
     {0x2F, 0x80, 0xEA}, {0x0F, 0x5C, 0xC4}, {0x14, 0x70, 0xE1}, {0x0F, 0x5C, 0xC4}},
}};

struct LegacyKey {
    const char *Key;
    ThemeKind Kind;
};

constexpr std::array<LegacyKey, 3> LegacyKeys = {{
    {"shadowworks", ThemeKind::Blue},
    {"monochrome", ThemeKind::Slate},
    {"light", ThemeKind::White},
}};

// 图表配色的候选：蓝、青、琥珀、紫、粉、绿。白色主题用深一档的，白底上才看得清。
constexpr std::array<Rgb, 6> DarkSeries = {{
    {0x5B, 0x9B, 0xF5}, {0x2F, 0xD0, 0xB5}, {0xF5, 0xA5, 0x24}, {0xA7, 0x8B, 0xFA}, {0xF4, 0x72, 0xB6}, {0x84, 0xCC, 0x16},
}};
constexpr std::array<Rgb, 6> LightSeries = {{
    {0x25, 0x63, 0xEB}, {0x0D, 0x94, 0x88}, {0xD9, 0x77, 0x06}, {0x7C, 0x3A, 0xED}, {0xDB, 0x27, 0x77}, {0x65, 0xA3, 0x0D},
}};

const ThemeSpec &spec_of(ThemeKind Kind) {
    const auto Found =
        std::find_if(Themes.begin(), Themes.end(), [Kind](const ThemeSpec &Spec) { return Spec.Kind == Kind; });
    return Found != Themes.end() ? *Found : Themes.front();
}

void fill_dark_grays(Palette &P) {
    P.Canvas = gray(0x20);
    P.Panel = gray(0x26);
    P.Side = gray(0x22);
    P.Divider = gray(0x33);
    P.Outline = gray(0x3A);
    P.Card = gray(0x27);
    P.Edge = gray(0x4D);
    P.Popup = gray(0x2E);
    P.Text = {0xE3, 0xE5, 0xE8};
    P.Text2 = {0xAF, 0xB4, 0xBC};
    P.Text3 = {0x8A, 0x8F, 0x96};
    P.Text4 = {0x6E, 0x72, 0x78};
    P.Icon = gray(0xD0);
    P.Danger = {0xF8, 0x71, 0x71};
    P.Success = {0x4A, 0xDE, 0x80};
    P.ControlHover = gray(0x3A);
    P.ControlPressed = gray(0x45);
    P.EdgeHover = gray(0x5C);
    P.Overlay = White;
    P.FieldEditing = gray(0x1A);
    P.CheckFill = gray(0x24);
    P.CheckEdge = gray(0x3C);
    P.TileAlpha = 0.18f;
    P.Light = false;
}

void fill_light_grays(Palette &P) {
    P.Canvas = {0xF3, 0xF4, 0xF6};
    P.Panel = White;
    P.Side = {0xF6, 0xF7, 0xF9};
    P.Divider = {0xE4, 0xE7, 0xEB};
    P.Outline = {0xD3, 0xD7, 0xDE};
    P.Card = White;
    P.Edge = {0xC6, 0xCB, 0xD2};
    P.Popup = White;
    P.Text = {0x1F, 0x23, 0x29};
    P.Text2 = {0x4B, 0x52, 0x60};
    P.Text3 = {0x6B, 0x72, 0x80};
    P.Text4 = {0x9A, 0xA0, 0xA8};
    P.Icon = {0x4B, 0x52, 0x60};
    P.Danger = {0xD6, 0x3B, 0x3B};
    P.Success = {0x16, 0xA3, 0x4A};
    P.ControlHover = {0xE3, 0xE6, 0xEA};
    P.ControlPressed = {0xD6, 0xDA, 0xE0};
    P.EdgeHover = {0x9E, 0xA5, 0xAF};
    P.Overlay = {0, 0, 0};
    P.FieldEditing = White;
    P.CheckFill = White;
    P.CheckEdge = {0xB4, 0xBA, 0xC2};
    P.TileAlpha = 1.0f;
    P.Light = true;
}

constexpr std::array<const char *, 14> FlatWidgets = {
    "button",    "tool_button", "toggle_button", "radio_button", "checkbox", "text_field", "number_field",
    "dropdown",  "menu",        "popup",         "menu_item",    "list_item", "box",       "tooltip",
};

struct Roundness {
    const char *Widget;
    double Value;
};

constexpr std::array<Roundness, 13> WidgetRoundness = {{
    {"button", 1.0},
    {"tool_button", 1.0},
    {"toggle_button", 1.0},
    {"text_field", 0.3},
    {"number_field", 0.3},
    {"dropdown", 0.3},
    {"menu", 0.3},
    {"menu_item", 0.3},
    {"list_item", 0.3},
    {"popup", 0.45},
    {"tooltip", 0.3},
    {"checkbox", 0.15},
    {"scrollbar", 1.0},
}};

struct BorderWidth {
    const char *Widget;
    double Pixels;
};

constexpr std::array<BorderWidth, 9> WidgetBorders = {{
    {"button", 0},
    {"tool_button", 0},
    {"toggle_button", 0},
    {"dropdown", 0},
    {"menu", 0},
    {"popup", 0},
    {"tooltip", 0},
    {"text_field", 1},
    {"checkbox", 1},
}};

Json colors_of(const Palette &P) {
    const Rgb MenuSelected = blend(P.Popup, P.AccentBright, 0.16f);
    const Rgb OnMenuSelected = P.Light ? P.Text : White;
    Json Colors = {
        {"background", P.Canvas.hex()},
        {"foreground", P.Text.hex()},
        {"border", P.Canvas.hex()},
        {"primary.background", P.AccentSolid.hex()},
        {"primary.foreground", White.hex()},
        {"secondary.background", P.Canvas.hex()},
        {"secondary.foreground", P.Text.hex()},
        {"accent.background", MenuSelected.hex()},
        {"accent.foreground", OnMenuSelected.hex()},
        {"popover.background", P.Popup.hex()},
        {"popover.foreground", P.Text.hex()},
        {"input.border", P.Canvas.hex()},
        {"panel.background", P.Panel.hex()},
        {"box.background", P.Card.hex()},
        {"list_item.background", P.Panel.hex()},
        {"button.background.selected", P.ControlPressed.hex()},
        {"button.text.selected", P.Text.hex()},
        {"tool_button.background.selected", P.ControlPressed.hex()},
        {"text_field.background", P.Canvas.hex()},
        {"text_field.background.selected", P.FieldEditing.hex()},
        {"text_field.border", P.Canvas.hex()},
        {"text_field.border.selected", P.AccentBright.hex()},
        {"text_field.text.selected", P.Text.hex()},
        {"checkbox.background", P.CheckFill.hex()},
        {"checkbox.border", P.CheckEdge.hex()},
        {"checkbox.border.selected", P.AccentSolid.hex()},
        {"checkbox.fill", White.hex()},
        {"checkbox.text", P.Text.hex()},
        {"checkbox.text.selected", P.Text.hex()},
        {"dropdown.background", P.Canvas.hex()},
        {"menu.background", P.Canvas.hex()},
        {"menu.background.selected", P.Canvas.hex()},
        {"menu.border", P.Canvas.hex()},
        {"menu.text", P.Text.hex()},
        {"menu.text.selected", P.Text.hex()},
        {"menu_item.text", P.Text.hex()},
        {"menu_item.text.selected", OnMenuSelected.hex()},
        {"popup.border", P.Popup.hex()},
        {"tooltip.border", P.Popup.hex()},
        {"caret", P.Text.hex()},
        {"selection.background", blend(P.Canvas, P.AccentBright, 0.34f).hex()},
        {"scrollbar.background", P.Panel.hex()},
        {"scrollbar.fill", P.Edge.hex()},
    };
    for (const char *Widget : FlatWidgets) {
        Colors[std::format("{}.gradient.enabled", Widget)] = false;
        Colors[std::format("{}.emboss.enabled", Widget)] = false;
    }
    for (const Roundness &Shape : WidgetRoundness) {
        Colors[std::format("{}.roundness", Shape.Widget)] = Shape.Value;
    }
    for (const BorderWidth &Border : WidgetBorders) {
        Colors[std::format("{}.border.width", Border.Widget)] = Border.Pixels;
    }
    return Colors;
}

} // namespace

std::string Rgb::hex() const {
    return std::format("#{:02x}{:02x}{:02x}", R, G, B);
}

Rgb blend(Rgb Base, Rgb Over, float Alpha) {
    const float Weight = std::clamp(Alpha, 0.0f, 1.0f);
    const auto mix = [Weight](std::uint8_t From, std::uint8_t To) {
        return static_cast<std::uint8_t>(std::lround(static_cast<float>(From) +
                                                     (static_cast<float>(To) - static_cast<float>(From)) * Weight));
    };
    return {mix(Base.R, Over.R), mix(Base.G, Over.G), mix(Base.B, Over.B)};
}

namespace {

float hue_of(Rgb Color) {
    const float R = static_cast<float>(Color.R) / 255.0f;
    const float G = static_cast<float>(Color.G) / 255.0f;
    const float B = static_cast<float>(Color.B) / 255.0f;
    return std::atan2(std::sqrt(3.0f) * (G - B), 2.0f * R - G - B);
}

float hue_distance(Rgb Left, Rgb Right) {
    constexpr float Pi = 3.14159265f;
    const float Gap = std::fabs(hue_of(Left) - hue_of(Right));
    return std::min(Gap, 2.0f * Pi - Gap);
}

/// 主题色打头，候选里和主题色色相最近的那个让位，其余按原顺序排在后面。
std::array<Rgb, 6> series_of(Rgb Accent, const std::array<Rgb, 6> &Candidates) {
    const auto Clash = std::min_element(Candidates.begin(), Candidates.end(), [Accent](Rgb Left, Rgb Right) {
        return hue_distance(Left, Accent) < hue_distance(Right, Accent);
    });
    std::array<Rgb, 6> Result = {Accent};
    std::size_t Next = 1;
    for (auto Candidate = Candidates.begin(); Candidate != Candidates.end(); ++Candidate) {
        if (Candidate != Clash) {
            Result[Next++] = *Candidate;
        }
    }
    return Result;
}

} // namespace

Palette palette_of(ThemeKind Kind) {
    const ThemeSpec &Spec = spec_of(Kind);
    Palette P;
    if (Spec.Light) {
        fill_light_grays(P);
    } else {
        fill_dark_grays(P);
    }
    P.Accent = Spec.Base;
    P.AccentDeep = Spec.Deep;
    P.AccentSolid = Spec.Solid;
    P.AccentHover = Spec.SolidHover;
    P.AccentPressed = Spec.SolidPressed;
    P.AccentBright = Spec.Bright;
    P.AccentOn = Spec.On;
    P.Series = series_of(Spec.Bright, Spec.Light ? LightSeries : DarkSeries);
    return P;
}

Rgb theme_swatch(ThemeKind Kind) {
    const ThemeSpec &Spec = spec_of(Kind);
    return Spec.Light ? White : Spec.Solid;
}

const char *theme_label(ThemeKind Kind) {
    return spec_of(Kind).Label;
}

const char *theme_key(ThemeKind Kind) {
    return spec_of(Kind).Key;
}

std::optional<ThemeKind> theme_from_key(std::string_view Key) {
    for (const ThemeSpec &Spec : Themes) {
        if (Key == Spec.Key) {
            return Spec.Kind;
        }
    }
    for (const LegacyKey &Legacy : LegacyKeys) {
        if (Key == Legacy.Key) {
            return Legacy.Kind;
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
        {"mode", spec_of(Kind).Light ? "light" : "dark"},
        {"radius", 0.3},
        {"shadow", false},
        {"font.size", 11},
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
            return theme_from_key(Found->get<std::string>()).value_or(ThemeKind::Blue);
        }
    } catch (const Error &) {
        // 文件缺失或损坏时回到默认主题，不阻止启动。
    }
    return ThemeKind::Blue;
}

void save_theme(const fs::path &Root, ThemeKind Kind) {
    atomic_write(Root / L"界面设置.json", dump_pretty(Json{{"theme", theme_key(Kind)}}));
}

} // namespace scriptroom::gui
