#include "Paint.h"

#include "BuiNode.h"

namespace scriptroom::gui::paint {

void fill(BuiRaster *Target, float X, float Y, float W, float H, Rgb Color, float Alpha) {
    float Rgba[4];
    to_rgba(Color, Alpha, Rgba);
    BUI_raster_fill_rect(Target, X, Y, W, H, Rgba);
}

void text(BuiRaster *Target, float X, float Y, float W, float H, const std::string &Text, Rgb Color, Align Where) {
    if (Text.empty() || W <= 0.0f) {
        return;
    }
    BKWidgetDraw Item = {};
    Item.kind = BK_WIDGET_LABEL;
    Item.align = static_cast<int>(Where);
    Item.rect[0] = X;
    Item.rect[1] = Y;
    Item.rect[2] = W;
    Item.rect[3] = H;
    Item.text_rect[0] = X;
    Item.text_rect[1] = Y;
    Item.text_rect[2] = W;
    Item.text_rect[3] = H;
    Item.clip[0] = X;
    Item.clip[1] = Y;
    Item.clip[2] = X + W;
    Item.clip[3] = Y + H;
    to_rgba(Color, 1.0f, Item.color);
    Item.text = Text.c_str();
    BK_draw_widget(Target, &Item);
}

float text_width(std::string_view Text) {
    return BK_text_width(Text.data(), static_cast<int>(Text.size()));
}

std::string_view utf8_prefix(std::string_view Text, std::size_t MaxBytes) {
    if (Text.size() <= MaxBytes) {
        return Text;
    }
    std::size_t End = MaxBytes;
    while (End > 0 && (static_cast<unsigned char>(Text[End]) & 0xC0U) == 0x80U) {
        --End;
    }
    return Text.substr(0, End);
}

} // namespace scriptroom::gui::paint
