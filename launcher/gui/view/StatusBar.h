#pragma once

#include "ViewServices.h"

namespace scriptroom::gui {

/// 底部状态栏：后台运行指示、提示文字、日志条数。
class StatusBar {
public:
    StatusBar(const ViewServices &Context, Node Parent);

    void sync();

private:
    const ViewServices &Services;
    Node Busy;
    Node Notice;
    Node Count;
};

} // namespace scriptroom::gui
