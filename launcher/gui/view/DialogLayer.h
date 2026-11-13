#pragma once

#include "TextBlock.h"
#include "ViewServices.h"

#include <cstdint>
#include <vector>

namespace scriptroom::gui {

/// 覆盖整个窗口的模态确认框：强制结束魔兽、退出时中断云服务。
///
/// 遮罩层是带 events 的整窗框架，点击落在遮罩上，下层控件收不到鼠标事件。
/// 两个对话框构建一次，之后只切换可见性与文字。
class DialogLayer {
public:
    DialogLayer(const ViewServices &Context, Node Root);

    void sync();

private:
    Node make_overlay(Node Root) const;
    Node make_card(Node Overlay, std::string_view Title) const;

    const ViewServices &Services;
    Node ForceOverlay;
    Node ExitOverlay;
    TextBlock ForceSummary;
    TextBlock ForcePids;
    TextBlock ExitMessage;
    std::vector<std::uint32_t> ShownPids;
};

} // namespace scriptroom::gui
