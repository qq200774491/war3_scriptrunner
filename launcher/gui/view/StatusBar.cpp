#include "StatusBar.h"

#include "Geometry.h"

#include <format>

namespace scriptroom::gui {

StatusBar::StatusBar(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    const Node Bar = Ui.frame(Parent, {.Side = Edge::Bottom, .Fill = Stretch::X, .Height = geometry::StatusHeight});
    Count = Ui.label(Bar, {.Side = Edge::Right, .Fill = Stretch::Y, .PadRight = 20, .Width = 110}, "",
                     {.Color = &Palette::Text3, .Align = "right"});
    Busy = Ui.label(Bar, {.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = 20}, "●", {.Color = &Palette::Success});
    Notice = Ui.label(Bar, {.Side = Edge::Left, .Fill = Stretch::Both, .Expand = true, .PadLeft = 8}, "",
                      {.Color = &Palette::Text3});
}

void StatusBar::sync() {
    const AppModel &Model = Services.Model;
    Busy.set_visible(Model.running() || Model.closing_games());
    Notice.set_text(Model.notice());
    Count.set_text(std::format("{} 条记录", Model.logs().size()));
}

} // namespace scriptroom::gui
