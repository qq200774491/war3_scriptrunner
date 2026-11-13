#include "RoundedCorners.h"

#include "CornerMask.h"
#include "Surface.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace scriptroom::gui {

namespace {

/// 软件渲染边画边写这块帧缓冲，BUI 只是没有给出可写的指针；在绘制回调里改写和画一个图元是一样的。
std::span<std::uint32_t> frame_pixels(BuiRaster *Target) {
    if (BUI_raster_is_gpu(Target) != 0 || BUI_raster_alpha(Target) == 0 || BUI_raster_pixels(Target) == nullptr) {
        return {};
    }
    const auto Count = static_cast<std::size_t>(BUI_raster_width(Target)) *
                       static_cast<std::size_t>(BUI_raster_height(Target));
    return {const_cast<std::uint32_t *>(BUI_raster_pixels(Target)), Count};
}

void place(Surface *Area, Node Root, double RelX, double RelY, double RelW, double RelH, int X, int Y, int W, int H) {
    BKNode *Raw = Area->node().raw();
    BK_node_add(Root.raw(), Raw, -1);
    BK_geom_set_manager(Raw, BK_GEOM_PLACE);
    BK_geom_set(Raw, "relx", RelX);
    BK_geom_set(Raw, "rely", RelY);
    BK_geom_set(Raw, "relwidth", RelW);
    BK_geom_set(Raw, "relheight", RelH);
    BK_geom_set(Raw, "x", X);
    BK_geom_set(Raw, "y", Y);
    BK_geom_set(Raw, "width", W);
    BK_geom_set(Raw, "height", H);
}

} // namespace

std::vector<Surface *> add_rounded_corners(Node Root, int Radius, std::function<bool()> Active,
                                           std::function<std::uint32_t()> Outline) {
    const auto IsActive = std::make_shared<std::function<bool()>>(std::move(Active));
    const auto Color = std::make_shared<std::function<std::uint32_t()>>(std::move(Outline));
    std::vector<Surface *> Created;

    constexpr std::array<Corner, 4> Corners = {Corner::TopLeft, Corner::TopRight, Corner::BottomLeft,
                                               Corner::BottomRight};
    for (const Corner Which : Corners) {
        Surface *Area = Surface::create([Which, Radius, IsActive, Color](BuiRaster *Target, const Surface::Box &,
                                                                         const Surface &) {
            const std::span<std::uint32_t> Pixels = frame_pixels(Target);
            if (!Pixels.empty() && (*IsActive)()) {
                fade_corner(Pixels, BUI_raster_width(Target), BUI_raster_height(Target), Which, Radius, (*Color)());
            }
        });
        const bool Right = Which == Corner::TopRight || Which == Corner::BottomRight;
        const bool Bottom = Which == Corner::BottomLeft || Which == Corner::BottomRight;
        place(Area, Root, Right ? 1.0 : 0.0, Bottom ? 1.0 : 0.0, 0.0, 0.0, Right ? -Radius : 0, Bottom ? -Radius : 0,
              Radius, Radius);
        Created.push_back(Area);
    }

    // 四条边各一根 1px 的节点，正好盖住要画描边的那一圈像素；它们在缩放边框里，不会挡住鼠标。
    constexpr std::array<Side, 4> Sides = {Side::Top, Side::Bottom, Side::Left, Side::Right};
    for (const Side Which : Sides) {
        Surface *Area = Surface::create([Which, Radius, IsActive, Color](BuiRaster *Target, const Surface::Box &,
                                                                         const Surface &) {
            const std::span<std::uint32_t> Pixels = frame_pixels(Target);
            if (!Pixels.empty() && (*IsActive)()) {
                outline_edge(Pixels, BUI_raster_width(Target), BUI_raster_height(Target), Which, Radius, (*Color)());
            }
        });
        const bool Horizontal = Which == Side::Top || Which == Side::Bottom;
        if (Horizontal) {
            place(Area, Root, 0.0, Which == Side::Bottom ? 1.0 : 0.0, 1.0, 0.0, Radius, Which == Side::Bottom ? -1 : 0,
                  -2 * Radius, 1);
        } else {
            place(Area, Root, Which == Side::Right ? 1.0 : 0.0, 0.0, 0.0, 1.0, Which == Side::Right ? -1 : 0, Radius,
                  1, -2 * Radius);
        }
        Created.push_back(Area);
    }
    return Created;
}

} // namespace scriptroom::gui
