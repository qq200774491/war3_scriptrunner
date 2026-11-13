#include "AccentPicker.h"

#include "Paint.h"

#include <optional>

namespace scriptroom::gui {

namespace {

constexpr int SwatchRadius = 9;
constexpr int SwatchPitch = 30;
constexpr int Margin = 6;
constexpr int PickerWidth = Margin * 2 + SwatchRadius * 2 + SwatchPitch * (static_cast<int>(AllThemes.size()) - 1);
constexpr int PickerHeight = 32;

float swatch_center(std::size_t Index) {
    return static_cast<float>(px(Margin + SwatchRadius + SwatchPitch * static_cast<int>(Index)));
}

std::optional<std::size_t> swatch_at(float X) {
    const float Reach = static_cast<float>(px(SwatchPitch)) * 0.5f;
    for (std::size_t Index = 0; Index < AllThemes.size(); ++Index) {
        if (X >= swatch_center(Index) - Reach && X < swatch_center(Index) + Reach) {
            return Index;
        }
    }
    return std::nullopt;
}

} // namespace

void add_accent_picker(const Builder &Ui, Node Parent, const Pack &Layout, AppModel &Model) {
    Pack Shape = Layout;
    Shape.Width = PickerWidth;
    Shape.Height = PickerHeight;
    const Styler &Colors = Ui.style();
    const auto paint_swatches = [&Colors, &Model](BuiRaster *Target, const Surface::Box &Area, const Surface &Self) {
        const Palette &P = Colors.palette();
        const Surface::Pointer &State = Self.pointer();
        const std::optional<std::size_t> Hovered = State.Hover ? swatch_at(State.X) : std::nullopt;
        const float Cy = Area.Y + Area.H * 0.5f;
        const float Radius = static_cast<float>(px(SwatchRadius));
        for (std::size_t Index = 0; Index < AllThemes.size(); ++Index) {
            const float Cx = Area.X + swatch_center(Index);
            if (AllThemes[Index] == Model.theme()) {
                paint::ring(Target, Cx, Cy, Radius + static_cast<float>(px(4)), static_cast<float>(px(2)), P.Text);
            } else if (Hovered == Index) {
                paint::ring(Target, Cx, Cy, Radius + static_cast<float>(px(4)), static_cast<float>(px(1)), P.Edge);
            }
            if (palette_of(AllThemes[Index]).Light) {
                paint::disc(Target, Cx, Cy, Radius, P.Edge);
                paint::disc(Target, Cx, Cy, Radius - static_cast<float>(px(1)), theme_swatch(AllThemes[Index]));
            } else {
                paint::disc(Target, Cx, Cy, Radius, theme_swatch(AllThemes[Index]));
            }
        }
    };
    const auto pick = [&Model](const Surface::Pointer &State) {
        if (const std::optional<std::size_t> Index = swatch_at(State.X)) {
            Model.set_theme(AllThemes[*Index]);
        }
    };
    Ui.surface(Parent, Shape, paint_swatches, pick)->node().str("description", "主题色");
}

} // namespace scriptroom::gui
