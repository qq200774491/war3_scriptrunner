#pragma once

#include "BuiNode.h"
#include "EventRouter.h"
#include "PillButton.h"
#include "Styler.h"
#include "Surface.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace scriptroom::gui {

struct TextStyle {
    ColorRole Color = &Palette::Text;
    double Scale = 1.0;
    bool Bold = false;
    const char *Align = "left"; ///< left / center / right
};

inline constexpr double TitleText = 1.15;

/// 构建界面时用的小工具：创建常用控件，并把颜色与事件登记到 Styler / EventRouter。
/// 所有 Parent 都必须是 frame 节点（pack 几何管理器的容器）。
class Builder {
public:
    Builder(Styler &Colors, EventRouter &Events) : Style(Colors), Router(Events) {}

    Styler &style() const { return Style; }
    EventRouter &router() const { return Router; }

    Node frame(Node Parent, const Pack &Layout, std::optional<ColorRole> Background = std::nullopt, int Padding = 0) const;

    Node card(Node Parent, const Pack &Layout, int Padding, ColorRole Fill = &Palette::Card, int Radius = 8) const;

    Node label(Node Parent, const Pack &Layout, std::string_view Text, const TextStyle &Look = {}) const;

    Node icon_button(Node Parent, const Pack &Layout, const char *Icon, std::string_view Tooltip,
                     std::function<void()> OnClick) const;

    PillButton pill(Node Parent, const Pack &Layout, std::string_view Text, Tone Kind,
                    std::function<void()> OnClick) const;

    Surface *surface(Node Parent, const Pack &Layout, Surface::Painter Paint,
                     std::function<void(const Surface::Pointer &)> OnClick = {}) const;

    Surface *backdrop(Node Parent, Surface::Painter Paint,
                      std::function<void(const Surface::Pointer &)> OnClick = {}) const;

    Node checkbox(Node Parent, const Pack &Layout, std::string_view Text, bool Checked,
                  std::function<void(bool)> OnChange) const;

    /// 单行输入框；OnChange 在内容变化时收到新文本（输入法组字结果与逐键输入都会触发）。
    Node text_field(Node Parent, const Pack &Layout, std::string_view Value,
                    std::function<void(const std::string &)> OnChange) const;

private:
    Surface::ClickHandler routed(std::function<void(const Surface::Pointer &)> OnClick) const;
    void repaint_on_theme(const Surface *Area) const;

    Styler &Style;
    EventRouter &Router;
};

} // namespace scriptroom::gui
