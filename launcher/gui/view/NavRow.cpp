#include "NavRow.h"

namespace scriptroom::gui {

NavRow::NavRow(const Builder &Ui, Node Parent, const Pack &Layout, const Look &Appearance,
               std::function<void()> OnClick)
    : Colors(Ui.style()), Selected(Appearance.SelectedFill), Hover(Appearance.HoverFill) {
    Pack Shape = Layout;
    Shape.Height = Appearance.Height;
    Frame = Ui.frame(Parent, Shape);
    Frame.num("events", 1.0);
    if (!Appearance.Tooltip.empty()) {
        Frame.str("description", Appearance.Tooltip);
    }

    const bool IconOnly = Appearance.Text.empty();
    if (Appearance.Icon != nullptr) {
        const TextStyle IconStyle{.Color = Appearance.IconColor, .Align = IconOnly ? "center" : "left"};
        const Pack Slot = IconOnly ? Pack{.Fill = Stretch::Both, .Expand = true}
                                   : Pack{.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = 6, .Width = 26};
        Ui.label(Frame, Slot, "", IconStyle).str("icon", Appearance.Icon);
    }
    if (!IconOnly) {
        Detail = Ui.label(Frame, {.Side = Edge::Right, .Fill = Stretch::Y, .PadRight = 8, .Width = 40}, "",
                          {.Color = &Palette::Muted, .Scale = SmallText, .Align = "right"});
        Ui.label(Frame, {.Side = Edge::Left, .Fill = Stretch::Both, .Expand = true, .PadLeft = Appearance.Icon ? 0 : 8},
                 Appearance.Text, {.Color = Appearance.TextColor});
    }

    EventRouter &Router = Ui.router();
    Router.on(Frame, BK_EV_ENTER, [this](double, double) {
        IsHover = true;
        refresh();
    });
    Router.on(Frame, BK_EV_LEAVE, [this](double, double) {
        IsHover = false;
        refresh();
    });
    Router.on(Frame, BK_EV_RELEASE, [this, Click = std::move(OnClick)](double X, double Y) {
        int Rect[4] = {};
        BK_node_rect(Frame.raw(), Rect);
        if (X >= 0 && Y >= 0 && X < Rect[2] && Y < Rect[3]) {
            Click();
        }
    });
    Colors.watch([this](const Palette &) { refresh(); });
}

void NavRow::set_selected(bool On) {
    if (IsSelected != On) {
        IsSelected = On;
        refresh();
    }
}

void NavRow::set_detail(std::string_view Text) {
    if (Detail) {
        Detail.set_text(Text);
    }
}

void NavRow::refresh() {
    if (IsSelected) {
        Frame.background(Colors.palette().*Selected);
    } else if (IsHover) {
        Frame.background(Colors.palette().*Hover);
    } else {
        Frame.no_background();
    }
}

} // namespace scriptroom::gui
