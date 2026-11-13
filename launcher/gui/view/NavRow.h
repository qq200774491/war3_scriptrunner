#pragma once

#include "Builder.h"

#include <functional>
#include <string>
#include <string_view>

namespace scriptroom::gui {

/// 一行可点击的导航项：整行响应悬停与点击，选中时换底色。
/// BUI 的标签不接收输入，所以做成带 events 的框架，再由 EventRouter 接收原始鼠标事件。
class NavRow {
public:
    struct Look {
        const char *Icon = nullptr;        ///< Blender 图标名；为空则无图标
        std::string Text;                  ///< 为空时只画图标（图标栏）
        std::string Tooltip;               ///< 悬停提示；为空则不设置
        ColorRole IconColor = &Palette::Muted;
        ColorRole TextColor = &Palette::Text;
        ColorRole SelectedFill = &Palette::Selected;
        ColorRole HoverFill = &Palette::Raised;
        int Height = 30;
    };

    NavRow(const Builder &Ui, Node Parent, const Pack &Layout, const Look &Appearance, std::function<void()> OnClick);
    NavRow(const NavRow &) = delete;
    NavRow &operator=(const NavRow &) = delete;

    void set_selected(bool On);
    /// 右侧的辅助文字（例如日志条数）；没有文字的行忽略。
    void set_detail(std::string_view Text);

private:
    void refresh();

    Node Frame;
    Node Detail;
    Styler &Colors;
    ColorRole Selected;
    ColorRole Hover;
    bool IsSelected = false;
    bool IsHover = false;
};

} // namespace scriptroom::gui
