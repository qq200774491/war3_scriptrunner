#include "Segmented.h"

#include "Geometry.h"
#include "Paint.h"

#include <memory>

namespace scriptroom::gui {

Segmented::Segmented(const Builder &Ui, Node Parent, Pack Layout, std::vector<std::string> Labels,
                     std::function<std::size_t()> Selected, std::function<void(std::size_t)> OnPick, int OptionWidth) {
    const auto Options = std::make_shared<const std::vector<std::string>>(std::move(Labels));
    const auto Count = static_cast<float>(Options->size());
    if (Layout.Width == 0) {
        Layout.Width = OptionWidth * static_cast<int>(Options->size());
    }
    if (Layout.Height == 0) {
        Layout.Height = geometry::ButtonHeight;
    }
    const Styler &Style = Ui.style();
    // 点击时要量节点宽度；本对象会被按值拷走，所以回调只捕获这个共享的格子。
    const auto Self = std::make_shared<Surface *>(nullptr);
    Area = Ui.surface(
        Parent, Layout,
        [Options, Count, Selected, &Style](BuiRaster *Target, const Surface::Box &Box, const Surface &) {
            const Palette &Colors = Style.palette();
            const std::size_t Chosen = Selected();
            const float Option = Box.W / Count;
            for (std::size_t Index = 0; Index < Options->size(); ++Index) {
                const float X = Box.X + Option * static_cast<float>(Index);
                if (Index == Chosen) {
                    paint::round_rect(Target, X, Box.Y, Option, Box.H, Box.H * 0.5f, Colors.Overlay, 0.12f);
                }
                paint::text(Target, X, Box.Y, Option, Box.H, (*Options)[Index], Index == Chosen ? Colors.Text : Colors.Text3,
                            paint::Align::Center);
            }
        },
        [Self, Count, OnPick = std::move(OnPick)](const Surface::Pointer &Where) {
            int Rect[4] = {};
            BK_node_rect((*Self)->node().raw(), Rect);
            if (Rect[2] > 0 && Where.X >= 0.0f) {
                const auto Index = static_cast<std::size_t>(Where.X * Count / static_cast<float>(Rect[2]));
                if (Index < static_cast<std::size_t>(Count)) {
                    OnPick(Index);
                }
            }
        });
    *Self = Area;
}

std::vector<std::string> count_labels(std::size_t First, std::size_t Last) {
    std::vector<std::string> Labels;
    for (std::size_t Value = First; Value <= Last; ++Value) {
        Labels.push_back(std::to_string(Value));
    }
    return Labels;
}

} // namespace scriptroom::gui
