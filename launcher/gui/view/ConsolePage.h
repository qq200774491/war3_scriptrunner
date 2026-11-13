#pragma once

#include "LogView.h"
#include "ViewServices.h"

namespace scriptroom::gui {

/// 控制台页：标题与跟随/清空、日志表；还没有日志时显示引导状态。
class ConsolePage {
public:
    ConsolePage(const ViewServices &Context, Node Parent);

    Node root() const { return Body; }
    void sync();

private:
    const ViewServices &Services;
    Node Body;
    Node FollowCheck;
    Node EmptyState;
    LogView Log;
};

} // namespace scriptroom::gui
