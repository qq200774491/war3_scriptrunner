#include "MainView.h"

#include "Error.h"

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

/// 窗口窄于此宽度（逻辑像素）时优先保留主工作区，隐藏详情栏。
constexpr int InspectorMinimumWindow = 1080;

} // namespace

MainView::MainView(AppModel &State, EventRouter &Events, Node Root, std::function<HWND()> Owner)
    : Model(State), Colors(palette_of(State.theme())), Ui(Colors, Events), Services{State, Ui, std::move(Owner)},
      AppliedTheme(State.theme()) {
    register_bui_themes();
    activate_bui_theme(AppliedTheme);
    Colors.background(Root, &Palette::Canvas);

    // pack 按添加顺序分配空间：先占边（顶、底、左、右），最后让工作区填满剩余部分。
    Header = std::make_unique<HeaderBar>(Services, Root);
    Status = std::make_unique<StatusBar>(Services, Root);
    Rail = std::make_unique<RailBar>(Services, Root);
    Navigation = std::make_unique<NavigationPanel>(Services, Root);
    Inspector = std::make_unique<InspectorPanel>(Services, Root);

    const Node Workspace = Ui.frame(Root, {.Fill = Stretch::Both, .Expand = true}, &Palette::Canvas);
    Actions = std::make_unique<ActionBar>(Services, Workspace);
    Console = std::make_unique<ConsolePage>(Services, Workspace);
    Settings = std::make_unique<SettingsPage>(Services, Workspace);
    Archives = std::make_unique<ArchivesPage>(Services, Workspace);

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
    const bool Wide = WindowWidth == 0 || WindowWidth >= px(InspectorMinimumWindow);
    Inspector->root().set_visible(Model.inspector_open() && Wide);
}

void MainView::resize(int Width, int) {
    WindowWidth = Width;
    update_inspector_visibility();
}

void MainView::sync() {
    apply_theme_if_changed();
    const std::optional<Config> Resolved = Model.resolved_config();

    Header->sync();
    Rail->sync();
    Navigation->sync();
    Actions->sync();
    Status->sync();

    Console->root().set_visible(Model.page() == Page::Console);
    Settings->root().set_visible(Model.page() == Page::Settings);
    Archives->root().set_visible(Model.page() == Page::Archives);
    Console->sync();
    Settings->sync();
    Archives->sync(Resolved);
    Inspector->sync(Resolved);
    update_inspector_visibility();
    Dialogs->sync();
}

} // namespace scriptroom::gui
