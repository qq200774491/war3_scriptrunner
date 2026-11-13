#pragma once

#include "NavRow.h"
#include "TextBlock.h"
#include "ViewServices.h"

#include <array>
#include <memory>

namespace scriptroom::gui {

/// 最左侧的图标栏：三个页面的入口。
class RailBar {
public:
    RailBar(const ViewServices &Context, Node Parent);

    void sync();

private:
    const ViewServices &Services;
    std::array<std::unique_ptr<NavRow>, 3> Rows;
};

/// 图标栏右侧的导航栏：日志范围（带计数）、项目页面、当前配置文件名。
class NavigationPanel {
public:
    NavigationPanel(const ViewServices &Context, Node Parent);

    void sync();

private:
    const ViewServices &Services;
    std::array<std::unique_ptr<NavRow>, 3> ScopeRows;
    std::array<std::unique_ptr<NavRow>, 2> PageRows;
    TextBlock ConfigName;
};

} // namespace scriptroom::gui
