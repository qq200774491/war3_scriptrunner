#include "HeaderBar.h"

#include <array>

namespace scriptroom::gui {

namespace {

int theme_index(ThemeKind Kind) {
    for (std::size_t Index = 0; Index < AllThemes.size(); ++Index) {
        if (AllThemes[Index] == Kind) {
            return static_cast<int>(Index);
        }
    }
    return 0;
}

} // namespace

HeaderBar::HeaderBar(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    const Node Bar = Ui.frame(Parent, {.Side = Edge::Top, .Fill = Stretch::X, .Height = 44}, &Palette::Panel, 8);

    Ui.label(Bar, {.Side = Edge::Left, .PadLeft = 4}, "", {.Color = &Palette::Accent}).str("icon", "SCRIPT");
    Ui.label(Bar, {.Side = Edge::Left, .PadLeft = 2}, "ScriptRoom", {.Scale = TitleText, .Bold = true});

    const Node Search = Bar.add(Node::create("search"), {.Side = Edge::Left, .PadLeft = 38, .Width = 240});
    Search.str("placeholder", "搜索日志");
    Search.str("value", Model.filter().Query);
    Ui.router().on_change(Search, [&Model, Search] { Model.set_query(Search.string("value")); });

    ThemeMenu = Bar.add(Node::create("dropdown"), {.Side = Edge::Right, .Width = 96});
    std::array<std::string, AllThemes.size()> Labels;
    std::array<const char *, AllThemes.size()> Items;
    for (std::size_t Index = 0; Index < AllThemes.size(); ++Index) {
        Labels[Index] = theme_label(AllThemes[Index]);
        Items[Index] = Labels[Index].c_str();
    }
    BK_set_strs(ThemeMenu.raw(), "items", Items.data(), static_cast<int>(Items.size()));
    ThemeMenu.set_value(theme_index(Model.theme()));
    ThemeMenu.str("description", "界面主题");
    Ui.router().on_change(ThemeMenu, [&Model, Menu = ThemeMenu] {
        const auto Index = static_cast<std::size_t>(Menu.number("value"));
        if (Index < AllThemes.size()) {
            Model.set_theme(AllThemes[Index]);
        }
    });

    InspectorToggle = Ui.button(Bar, {.Side = Edge::Right, .PadRight = 10}, "", "MENU_PANEL", [&Model] {
        Model.set_inspector_open(!Model.inspector_open());
    });
    InspectorToggle.str("description", "显示或隐藏详情");
}

void HeaderBar::sync() {
    ThemeMenu.set_value(theme_index(Services.Model.theme()));
}

} // namespace scriptroom::gui
