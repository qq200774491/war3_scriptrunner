#include "Navigation.h"

#include "Text.h"

namespace scriptroom::gui {

namespace {

struct RailItem {
    Page Target;
    const char *Icon;
    const char *Tooltip;
};

constexpr std::array<RailItem, 3> RailItems = {{
    {Page::Console, "CONSOLE", "云脚本控制台"},
    {Page::Settings, "PREFERENCES", "配置"},
    {Page::Archives, "PACKAGE", "玩家存档"},
}};

constexpr std::array<LogScope, 3> Scopes = {LogScope::All, LogScope::Cloud, LogScope::Errors};

struct ProjectItem {
    Page Target;
    const char *Icon;
    const char *Text;
};

constexpr std::array<ProjectItem, 2> ProjectItems = {{
    {Page::Settings, "PREFERENCES", "路径与配置"},
    {Page::Archives, "PACKAGE", "玩家存档"},
}};

void section_title(const Builder &Ui, Node Parent, std::string_view Text) {
    Ui.label(Parent, {.Fill = Stretch::X, .PadTop = 18, .PadBottom = 5}, Text,
             {.Color = &Palette::Muted, .Scale = SmallText});
}

} // namespace

RailBar::RailBar(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    const Node Rail = Ui.frame(Parent, {.Side = Edge::Left, .Fill = Stretch::Y, .Width = 48}, &Palette::Rail, 7);
    for (std::size_t Index = 0; Index < RailItems.size(); ++Index) {
        const RailItem &Item = RailItems[Index];
        NavRow::Look Appearance;
        Appearance.Icon = Item.Icon;
        Appearance.Tooltip = Item.Tooltip;
        Appearance.IconColor = &Palette::RailIcon;
        Appearance.SelectedFill = &Palette::RailSelected;
        Appearance.HoverFill = &Palette::RailSelected;
        Appearance.Height = 36;
        Rows[Index] = std::make_unique<NavRow>(Ui, Rail, Pack{.Fill = Stretch::X, .PadTop = 7}, Appearance,
                                               [&Model, Target = Item.Target] { Model.show_page(Target); });
    }
}

void RailBar::sync() {
    for (std::size_t Index = 0; Index < RailItems.size(); ++Index) {
        Rows[Index]->set_selected(Services.Model.page() == RailItems[Index].Target);
    }
}

NavigationPanel::NavigationPanel(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    const Node Panel = Ui.frame(Parent, {.Side = Edge::Left, .Fill = Stretch::Y, .Width = 164}, &Palette::Panel, 12);

    Ui.label(Panel, {.Side = Edge::Bottom, .Fill = Stretch::X}, "本地工作区", {.Color = &Palette::Muted, .Scale = SmallText});
    Ui.label(Panel, {.Fill = Stretch::X, .PadTop = 2}, "工作区", {.Scale = SmallText, .Bold = true});

    section_title(Ui, Panel, "控制台");
    for (std::size_t Index = 0; Index < Scopes.size(); ++Index) {
        NavRow::Look Appearance;
        Appearance.Text = scope_label(Scopes[Index]);
        ScopeRows[Index] = std::make_unique<NavRow>(Ui, Panel, Pack{.Fill = Stretch::X}, Appearance,
                                                    [&Model, Scope = Scopes[Index]] { Model.show_scope(Scope); });
    }

    section_title(Ui, Panel, "项目");
    for (std::size_t Index = 0; Index < ProjectItems.size(); ++Index) {
        NavRow::Look Appearance;
        Appearance.Icon = ProjectItems[Index].Icon;
        Appearance.Text = ProjectItems[Index].Text;
        PageRows[Index] = std::make_unique<NavRow>(Ui, Panel, Pack{.Fill = Stretch::X}, Appearance,
                                                   [&Model, Target = ProjectItems[Index].Target] { Model.show_page(Target); });
    }

    section_title(Ui, Panel, "当前配置");
    const Node ConfigIcon = Ui.frame(Panel, {.Fill = Stretch::X});
    Ui.label(ConfigIcon, {.Side = Edge::Left}, "", {.Color = &Palette::Muted}).str("icon", "FILE_TEXT");
    Panel.add(ConfigName.node(), {.Fill = Stretch::X, .PadLeft = 4});
    Ui.style().watch([this](const Palette &Colors) { ConfigName.set_color(Colors.Muted); });
}

void NavigationPanel::sync() {
    const AppModel &Model = Services.Model;
    for (std::size_t Index = 0; Index < Scopes.size(); ++Index) {
        ScopeRows[Index]->set_selected(Model.page() == Page::Console && Model.filter().Scope == Scopes[Index]);
        ScopeRows[Index]->set_detail(std::to_string(Model.logs().count(Scopes[Index])));
    }
    for (std::size_t Index = 0; Index < ProjectItems.size(); ++Index) {
        PageRows[Index]->set_selected(Model.page() == ProjectItems[Index].Target);
    }
    ConfigName.set_text(path_to_utf8(Model.config_path().filename()));
}

} // namespace scriptroom::gui
