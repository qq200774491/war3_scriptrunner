#include "TextBlock.h"

#include "Paint.h"
#include "TextWrap.h"

#include <algorithm>

namespace scriptroom::gui {

TextBlock::TextBlock() : Handle(Node::create("custom")) {
    BKCustomClass Class = {};
    Class.frame = &TextBlock::on_frame;
    Class.draw = &TextBlock::on_draw;
    Class.natural_w = px(40);
    Class.natural_h = line_height();
    BK_custom_set(Handle.raw(), &Class, this);
}

int TextBlock::line_height() const {
    return px(20);
}

void TextBlock::set_text(std::string_view NewText) {
    if (Text == NewText) {
        return;
    }
    Text = std::string(NewText);
    BK_custom_request_frame(Handle.raw(), 0);
}

void TextBlock::set_color(Rgb NewColor) {
    Color = NewColor;
    BK_node_invalidate(Handle.raw());
}

void TextBlock::rewrap() {
    int Rect[4] = {};
    BK_node_rect(Handle.raw(), Rect);
    Lines.clear();
    for (const std::string_view Line : wrap_text(Text, static_cast<float>(Rect[2]), paint::text_width)) {
        Lines.emplace_back(Line);
    }
    if (static_cast<int>(Lines.size()) > MaxLines) {
        Lines.resize(MaxLines);
        Lines.back() += "…";
    }
    // 把需要的高度告诉父布局；高度没变时 BK_geom_set 不会触发重排。
    const int Needed = std::max(1, static_cast<int>(Lines.size())) * line_height();
    BK_geom_set(Handle.raw(), "reqheight", Needed);
}

void TextBlock::on_frame(void *Self, BKNode *) {
    try {
        static_cast<TextBlock *>(Self)->rewrap();
    } catch (...) {
        // 折行失败时保留上一次的结果。
    }
}

void TextBlock::on_draw(void *Self, BKNode *, BuiRaster *Target, const int Rect[4]) {
    try {
        static_cast<TextBlock *>(Self)->draw(Target, Rect);
    } catch (...) {
        // 异常不得穿过 C 回调。
    }
}

void TextBlock::draw(BuiRaster *Target, const int Rect[4]) {
    // 宽度变化或文本变化时 BK_render 会先调用 frame（rewrap），所以这里总是画最新的折行结果。
    const float Height = static_cast<float>(line_height());
    for (std::size_t Index = 0; Index < Lines.size(); ++Index) {
        paint::text(Target, static_cast<float>(Rect[0]), static_cast<float>(Rect[1]) + Height * static_cast<float>(Index),
                    static_cast<float>(Rect[2]), Height, Lines[Index], Color);
    }
}

} // namespace scriptroom::gui
