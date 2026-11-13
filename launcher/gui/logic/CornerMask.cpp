#include "CornerMask.h"

#include <algorithm>
#include <cmath>

namespace scriptroom::gui {

namespace {

constexpr std::uint32_t Opaque = 0xFFu;
constexpr std::uint32_t AlmostOpaque = 0xFEu;

/// 中心在 (Dx, Dy) 处的像素落在半径 Radius 的圆里的比例，0..1。
float inside(float Dx, float Dy, float Radius) {
    return std::clamp(Radius + 0.5f - std::sqrt(Dx * Dx + Dy * Dy), 0.0f, 1.0f);
}

std::uint32_t channel(std::uint32_t Color, int Shift) {
    return (Color >> Shift) & 0xFFu;
}

/// 不透明的内容像素按面积混进描边与透明部分，结果是预乘的；alpha 至多 254，标记这个像素处理过了。
std::uint32_t blend(std::uint32_t Pixel, std::uint32_t Outline, float Content, float Ring) {
    const float Alpha = Content + Ring;
    const auto Limit = std::min(AlmostOpaque, static_cast<std::uint32_t>(Alpha * 255.0f + 0.5f));
    const float Fit = Alpha > 0.0f ? static_cast<float>(Limit) / (Alpha * 255.0f) : 0.0f;
    std::uint32_t Result = Limit << 24;
    for (int Shift = 0; Shift < 24; Shift += 8) {
        const float Mixed = static_cast<float>(channel(Pixel, Shift)) * Content +
                            static_cast<float>(channel(Outline, Shift)) * Ring;
        Result |= std::min(Limit, static_cast<std::uint32_t>(Mixed * Fit + 0.5f)) << Shift;
    }
    return Result;
}

std::uint32_t scale(std::uint32_t Pixel, float Coverage) {
    const auto Weight = static_cast<std::uint32_t>(Coverage * 255.0f + 0.5f);
    std::uint32_t Result = 0;
    for (int Shift = 0; Shift < 32; Shift += 8) {
        Result |= ((channel(Pixel, Shift) * Weight + 127u) / 255u) << Shift;
    }
    return Result;
}

bool fits(std::span<std::uint32_t> Pixels, int Width, int Height) {
    return Width > 0 && Height > 0 &&
           Pixels.size() >= static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height);
}

} // namespace

OutlineMode outline_mode(bool SystemCorners, bool Maximized) {
    if (SystemCorners) {
        return OutlineMode::System;
    }
    return Maximized ? OutlineMode::None : OutlineMode::Drawn;
}

void fade_corner(std::span<std::uint32_t> Pixels, int Width, int Height, Corner Which, int Radius,
                 std::optional<std::uint32_t> Outline) {
    const int Size = std::min({Radius, Width, Height});
    if (Size <= 0 || !fits(Pixels, Width, Height)) {
        return;
    }
    const bool Right = Which == Corner::TopRight || Which == Corner::BottomRight;
    const bool Bottom = Which == Corner::BottomLeft || Which == Corner::BottomRight;
    const auto Round = static_cast<float>(Radius);
    for (int Y = 0; Y < Size; ++Y) {
        const int Row = Bottom ? Height - 1 - Y : Y;
        for (int X = 0; X < Size; ++X) {
            const int Column = Right ? Width - 1 - X : X;
            std::uint32_t &Pixel = Pixels[static_cast<std::size_t>(Row) * static_cast<std::size_t>(Width) +
                                          static_cast<std::size_t>(Column)];
            if ((Pixel >> 24) != Opaque) {
                continue;
            }
            const float Dx = Round - (static_cast<float>(X) + 0.5f);
            const float Dy = Round - (static_cast<float>(Y) + 0.5f);
            const float Outer = inside(Dx, Dy, Round);
            if (!Outline) {
                if (Outer < 1.0f) {
                    Pixel = scale(Pixel, Outer);
                }
                continue;
            }
            const float Content = inside(Dx, Dy, Round - 1.0f);
            if (Outer - Content > 0.0f || Outer < 1.0f) {
                Pixel = blend(Pixel, *Outline, Content, Outer - Content);
            }
        }
    }
}

void outline_edge(std::span<std::uint32_t> Pixels, int Width, int Height, Side Which, int Radius,
                  std::uint32_t Outline) {
    if (!fits(Pixels, Width, Height)) {
        return;
    }
    const std::uint32_t Color = (Opaque << 24) | (Outline & 0xFFFFFFu);
    const bool Horizontal = Which == Side::Top || Which == Side::Bottom;
    const int Length = Horizontal ? Width : Height;
    const int Fixed = Which == Side::Bottom ? Height - 1 : (Which == Side::Right ? Width - 1 : 0);
    for (int Along = std::max(Radius, 0); Along < Length - std::max(Radius, 0); ++Along) {
        const int X = Horizontal ? Along : Fixed;
        const int Y = Horizontal ? Fixed : Along;
        Pixels[static_cast<std::size_t>(Y) * static_cast<std::size_t>(Width) + static_cast<std::size_t>(X)] = Color;
    }
}

} // namespace scriptroom::gui
