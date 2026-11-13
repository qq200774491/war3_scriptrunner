#pragma once

#include "BuiNode.h"
#include "Theme.h"

#include <string>
#include <string_view>
#include <vector>

namespace scriptroom::gui {

/// 自动折行的只读文本：BUI 的标签不换行，路径与日志全文需要它。
///
/// 高度随折行结果变化：节点在宽度确定后（BUI 的 frame 回调）重新折行，
/// 再通过 pack 的 reqheight 把需要的高度告诉父布局。只在界面线程使用。
class TextBlock {
public:
    /// 超过这个行数的文本只显示前面的部分，避免一条超长日志撑爆检查器。
    static constexpr int MaxLines = 40;

    TextBlock();
    TextBlock(const TextBlock &) = delete;
    TextBlock &operator=(const TextBlock &) = delete;

    Node node() const { return Handle; }

    void set_text(std::string_view Text);
    void set_color(Rgb Color);

private:
    static void on_frame(void *Self, BKNode *Source);
    static void on_draw(void *Self, BKNode *Source, BuiRaster *Target, const int Rect[4]);

    void rewrap();
    void draw(BuiRaster *Target, const int Rect[4]);
    int line_height() const;

    Node Handle;
    std::string Text;
    Rgb Color;
    std::vector<std::string> Lines; ///< 按最近一次的节点宽度折好的行
};

} // namespace scriptroom::gui
