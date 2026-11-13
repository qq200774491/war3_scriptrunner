#pragma once

#include "BuiNode.h"
#include "EventRouter.h"
#include "Styler.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace scriptroom::gui {

/// 文字样式：颜色角色、相对字号（1.0 是 BUI 默认的 11pt）、粗细与对齐。
struct TextStyle {
    ColorRole Color = &Palette::Text;
    double Scale = 1.0;
    bool Bold = false;
    const char *Align = "left"; ///< left / center / right
};

inline constexpr double SmallText = 0.9;
inline constexpr double TitleText = 1.15;

/// 构建界面时用的小工具：创建常用控件，并把颜色与事件登记到 Styler / EventRouter。
/// 所有 Parent 都必须是 frame 节点（pack 几何管理器的容器）。
class Builder {
public:
    Builder(Styler &Colors, EventRouter &Events) : Style(Colors), Router(Events) {}

    Styler &style() const { return Style; }
    EventRouter &router() const { return Router; }

    Node frame(Node Parent, const Pack &Layout, std::optional<ColorRole> Background = std::nullopt, int Padding = 0) const;

    /// 带 1 像素边框的卡片，底色由 Fill 指定；返回内层框架。
    Node card(Node Parent, const Pack &Layout, int Padding, ColorRole Fill = &Palette::Panel) const;

    /// 1 像素的分隔线。Vertical 为真时是竖线。
    Node divider(Node Parent, const Pack &Layout, bool Vertical = false) const;

    Node label(Node Parent, const Pack &Layout, std::string_view Text, const TextStyle &Look = {}) const;

    /// 图标名取自 Blender 图标集；Icon 为空表示没有图标。
    Node button(Node Parent, const Pack &Layout, std::string_view Text, const char *Icon,
                std::function<void()> OnClick) const;

    Node checkbox(Node Parent, const Pack &Layout, std::string_view Text, bool Checked,
                  std::function<void(bool)> OnChange) const;

    /// 单行输入框；OnChange 在内容变化时收到新文本（输入法组字结果与逐键输入都会触发）。
    Node text_field(Node Parent, const Pack &Layout, std::string_view Value,
                    std::function<void(const std::string &)> OnChange) const;

private:
    Styler &Style;
    EventRouter &Router;
};

} // namespace scriptroom::gui
