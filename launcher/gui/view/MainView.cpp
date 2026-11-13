#include "MainView.h"

#include "Error.h"
#include "Geometry.h"

#include <format>

namespace scriptroom::gui {

namespace {

/// 把三套主题的 JSON 注册进 BUI（BUI 的主题是进程级的）。
void register_bui_themes() {
    for (const ThemeKind Kind : AllThemes) {
        require(BK_theme_load_json(bui_theme_json(Kind).c_str()) >= 0,
                std::format("载入界面主题失败：{}", BK_theme_error()));
    }
}

void activate_bui_theme(ThemeKind Kind) {
    require(BK_set_theme(bui_theme_name(Kind).c_str()) != 0, "切换界面主题失败");
}

} // namespace

MainView::MainView(AppModel &State, EventRouter &Events, Node Root, std::function<HWND()> Owner)
    : Model(State), Colors(palette_of(State.theme())), Ui(Colors, Events), Services{State, Ui, std::move(Owner)},
      AppliedTheme(State.theme()) {
    register_bui_themes();
    activate_bui_theme(AppliedTheme);
    Colors.background(Root, &Palette::Panel);

    Rail = std::make_unique<RailBar>(Services, Root);
    const Node Shell = Ui.frame(Root, {.Fill = Stretch::Both, .Expand = true});
    Top = std::make_unique<TopBar>(Services, Shell);
    Navigation = std::make_unique<NavigationPanel>(Services, Shell);
    Inspector = std::make_unique<InspectorPanel>(Services, Shell);

    const Node Workspace = Ui.frame(Shell, {.Fill = Stretch::Both, .Expand = true});
    Status = std::make_unique<StatusBar>(Services, Workspace);
    Console = std::make_unique<ConsolePage>(Services, Workspace);
    Settings = std::make_unique<SettingsPage>(Services, Workspace);
    Archives = std::make_unique<ArchivesPage>(Services, Workspace);
    About = std::make_unique<AboutPage>(Services, Workspace);

    Dialogs = std::make_unique<DialogLayer>(Services, Root); // 必须最后创建：盖在所有内容之上
    sync();
}

void MainView::apply_theme_if_changed() {
    if (Model.theme() == AppliedTheme) {
        return;
    }
    AppliedTheme = Model.theme();
    activate_bui_theme(AppliedTheme);
    Colors.apply(palette_of(AppliedTheme));
}

void MainView::update_inspector_visibility() {
    const bool Wide = WindowWidth == 0 || WindowWidth >= px(geometry::InspectorMinimumWindow);
    const bool Shown = Model.inspector_open() && Wide;
    Inspector->root().set_visible(Shown);
    Top->set_inspector_visible(Shown);
}

void MainView::resize(int Width, int) {
    WindowWidth = Width;
    update_inspector_visibility();
}

void MainView::sync() {
    apply_theme_if_changed();
    const std::optional<Config> Resolved = Model.resolved_config();

    Top->sync();
    Rail->sync();
    Navigation->sync();
    Status->sync();

    Console->root().set_visible(Model.page() == Page::Console);
    Settings->root().set_visible(Model.page() == Page::Settings);
    Archives->root().set_visible(Model.page() == Page::Archives);
    About->root().set_visible(Model.page() == Page::About);
    Console->sync();
    Settings->sync();
    Archives->sync(Resolved);
    Inspector->sync(Resolved);
    update_inspector_visibility();
    Dialogs->sync();
}

} // namespace scriptroom::gui
