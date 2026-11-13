#include "NavRow.h"

#include "Geometry.h"
#include "Paint.h"

namespace scriptroom::gui {

namespace {

constexpr Rgb Black = {0, 0, 0};

constexpr int RowRadius = 8;
constexpr int IconSize = 22;
constexpr int IconInset = 8;
constexpr int CountRight = 10;
constexpr int CountWidth = 44;

}

NavRow::NavRow(const Builder &Ui, Node Parent, const Pack &Layout, const Look &Appearance,
               std::function<void()> OnClick)
    : Colors(Ui.style()), Tint(Appearance.Tint) {
    Pack Shape = Layout;
    Shape.Height = geometry::NavRowHeight;
    const Node Frame = Ui.frame(Parent, Shape);
    Area = Ui.backdrop(
        Frame, [this](BuiRaster *Target, const Surface::Box &Box, const Surface &Self) { paint(Target, Box, Self); },
        [Click = std::move(OnClick)](const Surface::Pointer &) { Click(); });

    IconLabel = Ui.label(Frame, {.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = IconInset, .Width = IconSize}, "",
                         {.Align = "center"});
    IconLabel.str("icon", Appearance.Icon);
    Title = Ui.label(Frame, {.Side = Edge::Left, .Fill = Stretch::Both, .Expand = true, .PadLeft = 8, .PadRight = CountWidth},
                     Appearance.Text);
    Colors.watch([this](const Palette &) { refresh_labels(); });
}

void NavRow::set_selected(bool On) {
    if (Selected != On) {
        Selected = On;
        refresh_labels();
        Area->invalidate();
    }
}

void NavRow::set_count(std::string_view Text) {
    if (Count != Text) {
        Count = std::string(Text);
        Area->invalidate();
    }
}

void NavRow::refresh_labels() const {
    Title.text_color(Colors.palette().Text);
    IconLabel.text_color(Tint);
}

void NavRow::paint(BuiRaster *Target, const Surface::Box &Box, const Surface &Self) const {
    const Palette &P = Colors.palette();
    const float Radius = static_cast<float>(px(RowRadius));
    if (Selected) {
        paint::round_rect(Target, Box.X, Box.Y, Box.W, Box.H, Radius, P.AccentSolid, P.Light ? 0.12f : 0.30f);
    } else if (Self.pointer().Hover) {
        paint::round_rect(Target, Box.X, Box.Y, Box.W, Box.H, Radius, P.Overlay, 0.06f);
    }
    if (!Count.empty()) {
        paint::mono_text(Target, Box.X, Box.Y, Box.W - static_cast<float>(px(CountRight)), Box.H, Count,
                         Selected ? P.Text : P.Text3, paint::Align::Right);
    }
}

RailKey::RailKey(const Builder &Ui, Node Parent, const Pack &Layout, const char *Icon, std::string_view Tooltip,
                 std::function<void()> OnClick)
    : Colors(Ui.style()) {
    Pack Shape = Layout;
    Shape.Fill = Stretch::X;
    Shape.Height = geometry::RailKeySize;
    const Node Frame = Ui.frame(Parent, Shape);
    Area = Ui.backdrop(
        Frame, [this](BuiRaster *Target, const Surface::Box &Box, const Surface &Self) { paint(Target, Box, Self); },
        [Click = std::move(OnClick)](const Surface::Pointer &) { Click(); });
    Area->node().str("description", Tooltip);
    Ui.label(Frame, {.Fill = Stretch::Both, .Expand = true}, "", {.Color = &Palette::Icon, .Align = "center"})
        .str("icon", Icon);
}

void RailKey::set_selected(bool On) {
    if (Selected != On) {
        Selected = On;
        Area->invalidate();
    }
}

void RailKey::paint(BuiRaster *Target, const Surface::Box &Box, const Surface &Self) const {
    const float Size = static_cast<float>(px(geometry::RailKeySize));
    const float Left = Box.X + (Box.W - Size) * 0.5f;
    const float Top = Box.Y + (Box.H - Size) * 0.5f;
    const float Radius = static_cast<float>(px(6));
    const Palette &P = Colors.palette();
    const Rgb Mark = P.Light ? P.AccentSolid : Black;
    if (Selected) {
        paint::round_rect(Target, Left, Top, Size, Size, Radius, Mark, P.Light ? 0.16f : 0.25f);
        const float MarkerHeight = static_cast<float>(px(24));
        paint::round_rect(Target, Box.X - static_cast<float>(px(2)), Box.Y + (Box.H - MarkerHeight) * 0.5f,
                          static_cast<float>(px(5)), MarkerHeight, static_cast<float>(px(2)), Mark,
                          P.Light ? 1.0f : 0.25f);
    } else if (Self.pointer().Hover) {
        paint::round_rect(Target, Left, Top, Size, Size, Radius, P.Overlay, P.Light ? 0.06f : 0.10f);
    }
}

} // namespace scriptroom::gui
