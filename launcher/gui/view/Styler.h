#pragma once

#include "BuiNode.h"
#include "Theme.h"

#include <functional>
#include <vector>

namespace scriptroom::gui {

/// 调色板中的一种颜色角色，例如 &Palette::Panel。
using ColorRole = Rgb Palette::*;

/// 记住“哪个节点用调色板里的哪种颜色”，切换主题时一次性重新着色。
///
/// 节点不保存颜色的来龙去脉，所以界面代码只说“背景用 Panel”，
/// 之后无论主题如何变化都不用再回头改这些节点。
class Styler {
public:
    explicit Styler(const Palette &Initial) : Current(Initial) {}
    Styler(const Styler &) = delete;
    Styler &operator=(const Styler &) = delete;

    const Palette &palette() const { return Current; }

    /// 立即应用并登记框架背景；Alpha 为 0 表示透明。
    void background(Node Target, ColorRole Role, float Alpha = 1.0f);
    /// 立即应用并登记文字颜色。
    void foreground(Node Target, ColorRole Role);
    /// 登记在主题变化时执行的任意回调（自绘视图、悬停状态等），并立即以当前调色板执行一次。
    void watch(std::function<void(const Palette &)> Reapply);

    /// 换调色板并让所有登记项重新着色。
    void apply(const Palette &Next);

private:
    struct Painted {
        Node Target;
        ColorRole Role;
        float Alpha;
        bool IsText;
    };

    void paint(const Painted &Item) const;

    Palette Current;
    std::vector<Painted> Items;
    std::vector<std::function<void(const Palette &)>> Watchers;
};

} // namespace scriptroom::gui
