#pragma once

#include "Builder.h"

#include <functional>
#include <string>
#include <vector>

namespace scriptroom::gui {

/// 一排互斥的小选项（时间范围、客户端个数），选中的那个垫一层淡底色。选中项每次绘制时向 Selected 要。
class Segmented {
public:
    Segmented() = default;
    /// Layout 的宽度为 0 时按每项 OptionWidth 逻辑像素排开。
    Segmented(const Builder &Ui, Node Parent, Pack Layout, std::vector<std::string> Labels,
              std::function<std::size_t()> Selected, std::function<void(std::size_t)> OnPick, int OptionWidth = 64);

    Node node() const { return Area->node(); }
    void refresh() const { Area->invalidate(); }

private:
    Surface *Area = nullptr;
};

} // namespace scriptroom::gui
