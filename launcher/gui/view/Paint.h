#pragma once

#include "Theme.h"

#include "BUI_kit.h"

#include <string>
#include <string_view>

namespace scriptroom::gui::paint {

/// 自绘节点（custom）共用的绘制原语。坐标与尺寸都是物理像素，
/// 只能在 BKCustomClass 的 draw 回调里使用。

enum class Align { Left = 0, Center = 1, Right = 2 };

void fill(BuiRaster *Target, float X, float Y, float W, float H, Rgb Color, float Alpha = 1.0f);

void round_rect(BuiRaster *Target, float X, float Y, float W, float H, float Radius, Rgb Color, float Alpha = 1.0f);

void disc(BuiRaster *Target, float CenterX, float CenterY, float Radius, Rgb Color, float Alpha = 1.0f);

void ring(BuiRaster *Target, float CenterX, float CenterY, float Radius, float Width, Rgb Color, float Alpha = 1.0f);

void line(BuiRaster *Target, float X1, float Y1, float X2, float Y2, float Width, Rgb Color, float Alpha = 1.0f);

void text(BuiRaster *Target, float X, float Y, float W, float H, const std::string &Text, Rgb Color,
          Align Where = Align::Left, float Alpha = 1.0f);

float text_width(std::string_view Text);

void mono_text(BuiRaster *Target, float X, float Y, float W, float H, std::string_view Text, Rgb Color,
               Align Where = Align::Left, float LogicalSize = 14.0f);

/// 取最多 MaxBytes 字节的前缀，不拆开 UTF-8 字符。超长的日志行只需绘制开头。
std::string_view utf8_prefix(std::string_view Text, std::size_t MaxBytes);

} // namespace scriptroom::gui::paint
