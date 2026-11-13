#pragma once

#include "AppModel.h"
#include "Builder.h"

#include <functional>

#include <windows.h>

namespace scriptroom::gui {

/// 各个面板共用的依赖：状态模型、控件构建器、对话框的父窗口。
/// 面板只持有它的引用，生命周期由 MainView 保证长于所有面板。
struct ViewServices {
    AppModel &Model;
    const Builder &Ui;
    /// 系统对话框的父窗口；截图模式下没有窗口，返回空。
    std::function<HWND()> Owner;
};

} // namespace scriptroom::gui
