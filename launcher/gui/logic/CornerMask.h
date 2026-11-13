#pragma once

#include <cstdint>
#include <span>

namespace scriptroom::gui {

enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };

/// 帧缓冲是预乘的 0xAARRGGBB（自上而下、行紧密排列）。把 Which 这个角上半径 Radius 的圆弧之外的像素
/// 淡成透明，弧线上的像素按覆盖率保留，边缘因此是抗锯齿的。
///
/// 只改动完全不透明的像素：淡过的像素不再是不透明的，所以重复调用不会把同一个像素淡两次，
/// 只重画了一部分区域的帧也只会淡新画的像素。Pixels 的长度不足 Width * Height 时什么都不做。
void fade_corner(std::span<std::uint32_t> Pixels, int Width, int Height, Corner Which, int Radius);

} // namespace scriptroom::gui
