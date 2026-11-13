#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace scriptroom::gui {

enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };
enum class Side { Top, Bottom, Left, Right };

/// 窗口外沿的 1px 描边由谁画：系统给窗口画圆角时（Win11）交给 DWM 的边框颜色；最大化时谁都不画。
enum class OutlineMode { None, Drawn, System };

OutlineMode outline_mode(bool SystemCorners, bool Maximized);

/// 帧缓冲是预乘的 0xAARRGGBB（自上而下、行紧密排列）。把 Which 这个角上半径 Radius 的圆弧之外的像素
/// 淡成透明，弧线上的像素按覆盖率保留，边缘因此是抗锯齿的。Outline 有值（0x00RRGGBB）时再沿圆弧画一圈
/// 1px 描边，描边环、内容和透明部分按各自占的面积混合。
///
/// 只改动完全不透明的像素，改过的像素都不再是完全不透明的（描边上本该不透明的像素记成 alpha 254，看不出来），
/// 所以重复调用不会把同一个像素处理两次，只重画了一部分区域的帧也只会处理新画的像素。
/// Pixels 的长度不足 Width * Height 时什么都不做。
void fade_corner(std::span<std::uint32_t> Pixels, int Width, int Height, Corner Which, int Radius,
                 std::optional<std::uint32_t> Outline = std::nullopt);

/// 把 Which 这一边最外一圈像素换成不透明的描边色（0x00RRGGBB），两头各让出 Radius 留给圆角。重复调用结果不变。
void outline_edge(std::span<std::uint32_t> Pixels, int Width, int Height, Side Which, int Radius,
                  std::uint32_t Outline);

} // namespace scriptroom::gui
