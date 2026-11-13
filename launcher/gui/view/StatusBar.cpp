#include "StatusBar.h"

#include <format>

namespace scriptroom::gui {

StatusBar::StatusBar(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    const Node Bar = Ui.frame(Parent, {.Side = Edge::Bottom, .Fill = Stretch::X, .Height = 26}, &Palette::Panel, 4);
    Count = Ui.label(Bar, {.Side = Edge::Right, .Fill = Stretch::Y, .Width = 90}, "", {.Color = &Palette::Muted, .Scale = SmallText, .Align = "right"});
    Busy = Ui.label(Bar, {.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = 6}, "●", {.Color = &Palette::Accent, .Scale = SmallText});
    Notice = Ui.label(Bar, {.Side = Edge::Left, .Fill = Stretch::Both, .Expand = true}, "", {.Color = &Palette::Muted, .Scale = SmallText});
}

void StatusBar::sync() {
    const AppModel &Model = Services.Model;
    Busy.set_visible(Model.running() || Model.closing_games());
    Notice.set_text(Model.notice());
    Count.set_text(std::format("{} 条记录", Model.logs().size()));
}

} // namespace scriptroom::gui
