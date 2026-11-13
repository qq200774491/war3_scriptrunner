#pragma once

#include "AboutPage.h"
#include "AppModel.h"
#include "ArchivesPage.h"
#include "Builder.h"
#include "ConsolePage.h"
#include "DialogLayer.h"
#include "EventRouter.h"
#include "InspectorPanel.h"
#include "Navigation.h"
#include "SettingsPage.h"
#include "StatsPage.h"
#include "StatusBar.h"
#include "Styler.h"
#include "TopBar.h"
#include "ViewServices.h"

#include <functional>
#include <memory>

namespace scriptroom::gui {

/// 整个窗口内容：把 AppModel 的状态同步到各个面板，并随窗口宽度显示或隐藏详情栏。
/// 节点树归调用方所有，必须比 MainView 活得久。
class MainView {
public:
    /// 在 Root（调用方持有的空 frame 节点）里构建全部界面。
    /// 必须在 BUI 的窗口创建之后构建，这样像素换算才取得正确的缩放。
    /// Owner 返回对话框的父窗口，可为空函数（截图模式没有窗口）。
    MainView(AppModel &State, EventRouter &Events, Node Root, std::function<HWND()> Owner);
    MainView(const MainView &) = delete;
    MainView &operator=(const MainView &) = delete;

    /// 把模型状态同步到控件。每次事件、唤醒或定时器之后调用。
    void sync();

    /// 窗口客户区尺寸变化（物理像素）。
    void resize(int Width, int Height);

private:
    void apply_theme_if_changed();
    void update_inspector_visibility();

    AppModel &Model;
    Styler Colors;
    Builder Ui;
    ViewServices Services;

    std::unique_ptr<RailBar> Rail;
    std::unique_ptr<TopBar> Top;
    std::unique_ptr<NavigationPanel> Navigation;
    std::unique_ptr<InspectorPanel> Inspector;
    std::unique_ptr<StatusBar> Status;
    std::unique_ptr<ConsolePage> Console;
    std::unique_ptr<StatsPage> Stats;
    std::unique_ptr<SettingsPage> Settings;
    std::unique_ptr<ArchivesPage> Archives;
    std::unique_ptr<AboutPage> About;
    std::unique_ptr<DialogLayer> Dialogs;

    ThemeKind AppliedTheme;
    int WindowWidth = 0;
};

} // namespace scriptroom::gui
