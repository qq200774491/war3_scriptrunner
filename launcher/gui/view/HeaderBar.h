#pragma once

#include "ViewServices.h"

namespace scriptroom::gui {

/// 顶部栏：品牌、日志搜索、显示/隐藏详情、主题选择。
class HeaderBar {
public:
    HeaderBar(const ViewServices &Context, Node Parent);

    void sync();

private:
    const ViewServices &Services;
    Node InspectorToggle;
    Node ThemeMenu;
};

} // namespace scriptroom::gui
