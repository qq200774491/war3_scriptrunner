#include "Paint.h"

#include "BuiNode.h"
#include "Fonts.h"

#include <algorithm>
#include <cmath>

namespace scriptroom::gui::paint {

namespace {

struct Color4 {
    float Rgba[4];
};

Color4 color_of(Rgb Color, float Alpha) {
    Color4 Result;
    to_rgba(Color, Alpha, Result.Rgba);
    return Result;
}

}

void fill(BuiRaster *Target, float X, float Y, float W, float H, Rgb Color, float Alpha) {
    BUI_raster_fill_rect(Target, X, Y, W, H, color_of(Color, Alpha).Rgba);
}

void round_rect(BuiRaster *Target, float X, float Y, float W, float H, float Radius, Rgb Color, float Alpha) {
    const float Clamped = std::min(Radius, std::min(W, H) * 0.5f);
    BUI_raster_round_rect(Target, X, Y, W, H, Clamped, color_of(Color, Alpha).Rgba);
}

void disc(BuiRaster *Target, float CenterX, float CenterY, float Radius, Rgb Color, float Alpha) {
    BUI_raster_circle(Target, CenterX, CenterY, Radius, 0.0f, color_of(Color, Alpha).Rgba);
}

void ring(BuiRaster *Target, float CenterX, float CenterY, float Radius, float Width, Rgb Color, float Alpha) {
    BUI_raster_circle(Target, CenterX, CenterY, Radius, Width, color_of(Color, Alpha).Rgba);
}

void line(BuiRaster *Target, float X1, float Y1, float X2, float Y2, float Width, Rgb Color, float Alpha) {
    BUI_raster_line(Target, X1, Y1, X2, Y2, Width, color_of(Color, Alpha).Rgba);
}

void text(BuiRaster *Target, float X, float Y, float W, float H, const std::string &Text, Rgb Color, Align Where,
          float Alpha) {
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
    to_rgba(Color, Alpha, Item.color);
    Item.text = Text.c_str();
    BK_draw_widget(Target, &Item);
}

float text_width(std::string_view Text) {
    return BK_text_width(Text.data(), static_cast<int>(Text.size()));
}

void mono_text(BuiRaster *Target, float X, float Y, float W, float H, std::string_view Text, Rgb Color, Align Where,
               float LogicalSize) {
    if (Text.empty() || W <= 0.0f) {
        return;
    }
    const float Size = LogicalSize * BK_scale();
    const int Length = static_cast<int>(Text.size());
    float Width = 0.0f;
    BUI_text_measure(MonoFamily, 0, Size, Text.data(), Length, &Width);
    float Left = X;
    if (Where == Align::Center) {
        Left = X + (W - Width) * 0.5f;
    } else if (Where == Align::Right) {
        Left = X + W - Width;
    }
    const float Baseline = Y + H * 0.5f + Size * 0.35f;
    BUI_raster_set_font(Target, MonoFamily, 0);
    BUI_raster_text(Target, std::floor(Left + 0.5f), std::floor(Baseline + 0.5f), Text.data(), Length, Size,
                    color_of(Color, 1.0f).Rgba);
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
