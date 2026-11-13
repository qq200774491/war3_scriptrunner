#include "Navigation.h"

#include "Geometry.h"

namespace scriptroom::gui {

namespace {

struct RailItem {
    Page Target;
    const char *Icon;
    const char *Tooltip;
    Edge Side;
};

// 底组按加入顺序从下往上排：设置在最下面，关于在它上面。
constexpr std::array<RailItem, 4> RailItems = {{
    {Page::Console, "CONSOLE", "云脚本控制台", Edge::Top},
    {Page::Archives, "PACKAGE", "玩家存档", Edge::Top},
    {Page::Settings, "PREFERENCES", "设置", Edge::Bottom},
    {Page::About, "INFO", "关于", Edge::Bottom},
}};

struct ScopeItem {
    LogScope Scope;
    const char *Icon;
    Rgb Tint;
};

constexpr std::array<ScopeItem, 3> ScopeItems = {{
    {LogScope::All, "ALIGN_JUSTIFY", {0x5B, 0x9B, 0xF5}},
    {LogScope::Cloud, "SCRIPT", {0x2F, 0xD0, 0xB5}},
    {LogScope::Errors, "ERROR", {0xF8, 0x71, 0x71}},
}};

} // namespace

RailBar::RailBar(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    const Node Rail = Ui.frame(Parent, {.Side = Edge::Left, .Fill = Stretch::Y, .Width = geometry::RailWidth},
                               &Palette::Accent);
    for (std::size_t Index = 0; Index < RailItems.size(); ++Index) {
        const RailItem &Item = RailItems[Index];
        const Pack Slot = Item.Side == Edge::Top ? Pack{.Side = Edge::Top, .PadTop = 18}
                                                 : Pack{.Side = Edge::Bottom, .PadBottom = 17};
        Keys[Index] = std::make_unique<RailKey>(Ui, Rail, Slot, Item.Icon, Item.Tooltip,
                                                [&Model, Target = Item.Target] { Model.show_page(Target); });
    }
}

void RailBar::sync() {
    for (std::size_t Index = 0; Index < RailItems.size(); ++Index) {
        Keys[Index]->set_selected(Services.Model.page() == RailItems[Index].Target);
    }
}

NavigationPanel::NavigationPanel(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    const Node Panel = Ui.frame(Parent, {.Side = Edge::Left, .Fill = Stretch::Y, .Width = geometry::SideWidth});

    const Node Head = Ui.frame(Panel, {.Side = Edge::Top, .Fill = Stretch::X, .Height = geometry::ToolbarHeight});
    const Node Body = Ui.frame(Panel, {.Fill = Stretch::Both, .Expand = true}, std::nullopt, 12);
    Actions = std::make_unique<ActionBar>(Services, Head, Body);

    const Node Scopes = Ui.frame(Body, {.Fill = Stretch::X, .PadTop = 22});
    for (std::size_t Index = 0; Index < ScopeItems.size(); ++Index) {
        const ScopeItem &Item = ScopeItems[Index];
        NavRow::Look Appearance{.Icon = Item.Icon, .Text = scope_label(Item.Scope), .Tint = Item.Tint};
        ScopeRows[Index] = std::make_unique<NavRow>(Ui, Scopes, Pack{.Fill = Stretch::X, .PadBottom = 2}, Appearance,
                                                    [&Model, Scope = Item.Scope] { Model.show_scope(Scope); });
    }
}

void NavigationPanel::sync() {
    const AppModel &Model = Services.Model;
    Actions->sync();
    for (std::size_t Index = 0; Index < ScopeItems.size(); ++Index) {
        const LogScope Scope = ScopeItems[Index].Scope;
        ScopeRows[Index]->set_selected(Model.page() == Page::Console && Model.filter().Scope == Scope);
        ScopeRows[Index]->set_count(std::to_string(Model.logs().count(Scope)));
    }
}

} // namespace scriptroom::gui
