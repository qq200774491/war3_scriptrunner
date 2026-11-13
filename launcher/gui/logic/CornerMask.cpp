#include "CornerMask.h"

#include <algorithm>
#include <cmath>

namespace scriptroom::gui {

namespace {

constexpr std::uint32_t Opaque = 0xFFu;

/// 角上第 (X, Y) 个像素（从角尖数起）落在圆角里的比例，0..255。
std::uint32_t coverage(int X, int Y, int Radius) {
    const float Dx = static_cast<float>(Radius) - (static_cast<float>(X) + 0.5f);
    const float Dy = static_cast<float>(Radius) - (static_cast<float>(Y) + 0.5f);
    const float Inside = std::clamp(static_cast<float>(Radius) + 0.5f - std::sqrt(Dx * Dx + Dy * Dy), 0.0f, 1.0f);
    return static_cast<std::uint32_t>(Inside * 255.0f + 0.5f);
}

std::uint32_t scale(std::uint32_t Pixel, std::uint32_t Coverage) {
    std::uint32_t Result = 0;
    for (int Shift = 0; Shift < 32; Shift += 8) {
        const std::uint32_t Channel = (Pixel >> Shift) & 0xFFu;
        Result |= ((Channel * Coverage + 127u) / 255u) << Shift;
    }
    return Result;
}

} // namespace

void fade_corner(std::span<std::uint32_t> Pixels, int Width, int Height, Corner Which, int Radius) {
    const int Size = std::min({Radius, Width, Height});
    if (Size <= 0 || Pixels.size() < static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height)) {
        return;
    }
    const bool Right = Which == Corner::TopRight || Which == Corner::BottomRight;
    const bool Bottom = Which == Corner::BottomLeft || Which == Corner::BottomRight;
    for (int Y = 0; Y < Size; ++Y) {
        const int Row = Bottom ? Height - 1 - Y : Y;
        for (int X = 0; X < Size; ++X) {
            const int Column = Right ? Width - 1 - X : X;
            std::uint32_t &Pixel = Pixels[static_cast<std::size_t>(Row) * static_cast<std::size_t>(Width) +
                                          static_cast<std::size_t>(Column)];
            const std::uint32_t Coverage = coverage(X, Y, Radius);
            if ((Pixel >> 24) == Opaque && Coverage < Opaque) {
                Pixel = scale(Pixel, Coverage);
            }
        }
    }
}

} // namespace scriptroom::gui
