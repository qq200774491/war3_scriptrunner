#pragma once

#include "ViewServices.h"

namespace scriptroom::gui {

/// 工作区顶部的操作栏：启动、检查、停止，以及右侧的“关闭全部魔兽”。
class ActionBar {
public:
    ActionBar(const ViewServices &Context, Node Parent);

    void sync();

private:
    const ViewServices &Services;
    Node StartButton;
    Node CheckButton;
    Node StopButton;
    Node CloseGamesButton;
};

} // namespace scriptroom::gui
