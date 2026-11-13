#include "TopBar.h"

#include "AppIcon.h"
#include "Geometry.h"
#include "WindowControls.h"

namespace scriptroom::gui {

namespace {

constexpr int BrandIconSize = 26;
constexpr int CompactRightWidth = 120;

} // namespace

TopBar::TopBar(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    const Node Bar = Ui.frame(Parent, {.Side = Edge::Top, .Fill = Stretch::X, .Height = geometry::TopBarHeight});

    build_brand(Bar);

    RightSegment = Ui.frame(Bar, {.Side = Edge::Right, .Fill = Stretch::Y, .Width = geometry::SideWidth});
    add_window_controls(Ui, RightSegment, {.Side = Edge::Right, .Fill = Stretch::Y, .PadRight = 21}, Services.Owner);

    const Node Center = Ui.frame(Bar, {.Fill = Stretch::Both, .Expand = true});
    Ui.icon_button(Center, {.Side = Edge::Right, .PadRight = 14}, "MENU_PANEL", "显示或隐藏详情",
                   [&Model] { Model.set_inspector_open(!Model.inspector_open()); });
    Search = Center.add(Node::create("search"),
                        {.Side = Edge::Right, .PadRight = 10, .Width = 300, .Height = geometry::ButtonHeight + 4});
    Search.str("placeholder", "搜索日志");
    Search.str("value", Model.filter().Query);
    Ui.router().on_change(Search, [&Model, Field = Search] { Model.set_query(Field.string("value")); });
}

void TopBar::build_brand(Node Bar) {
    const Builder &Ui = Services.Ui;
    const Node Brand = Ui.frame(Bar, {.Side = Edge::Left, .Fill = Stretch::Y, .Width = geometry::SideWidth});
    const Node Mark = Ui.label(Brand, {.Side = Edge::Left, .PadLeft = 18}, "");
    const int Size = px(BrandIconSize);
    const std::vector<std::uint8_t> Pixels = app_icon_rgba(Size);
    if (!Pixels.empty()) {
        BK_node_set_image(Mark.raw(), Pixels.data(), Size, Size);
    }
    Ui.label(Brand, {.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = 10}, "云脚本Test启动器",
             {.Scale = TitleText, .Bold = true});
}

void TopBar::set_inspector_visible(bool Shown) {
    if (InspectorShown == Shown) {
        return;
    }
    InspectorShown = Shown;
    RightSegment.set_pack({.Side = Edge::Right, .Fill = Stretch::Y,
                           .Width = Shown ? geometry::SideWidth : CompactRightWidth});
}

void TopBar::sync() {
    Search.set_visible(Services.Model.page() == Page::Console);
}

} // namespace scriptroom::gui
