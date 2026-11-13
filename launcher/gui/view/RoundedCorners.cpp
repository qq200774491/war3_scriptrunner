#include "RoundedCorners.h"

#include "CornerMask.h"
#include "Surface.h"

#include <array>
#include <cstdint>
#include <memory>

namespace scriptroom::gui {

namespace {

void fade(BuiRaster *Target, Corner Which, int Radius) {
    if (BUI_raster_is_gpu(Target) != 0 || BUI_raster_alpha(Target) == 0) {
        return;
    }
    const int Width = BUI_raster_width(Target);
    const int Height = BUI_raster_height(Target);
    // 软件渲染边画边写这块帧缓冲，BUI 只是没有给出可写的指针；在绘制回调里改写和画一个图元是一样的。
    auto *Pixels = const_cast<std::uint32_t *>(BUI_raster_pixels(Target));
    if (Pixels != nullptr) {
        fade_corner({Pixels, static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height)}, Width, Height,
                    Which, Radius);
    }
}

} // namespace

void add_rounded_corners(Node Root, int Radius, std::function<bool()> Active) {
    const auto IsActive = std::make_shared<std::function<bool()>>(std::move(Active));
    constexpr std::array<Corner, 4> Corners = {Corner::TopLeft, Corner::TopRight, Corner::BottomLeft,
                                               Corner::BottomRight};
    for (const Corner Which : Corners) {
        const Surface *Area = Surface::create([Which, Radius, IsActive](BuiRaster *Target, const Surface::Box &,
                                                                        const Surface &) {
            if ((*IsActive)()) {
                fade(Target, Which, Radius);
            }
        });
        const bool Right = Which == Corner::TopRight || Which == Corner::BottomRight;
        const bool Bottom = Which == Corner::BottomLeft || Which == Corner::BottomRight;
        BKNode *Raw = Area->node().raw();
        BK_node_add(Root.raw(), Raw, -1);
        BK_geom_set_manager(Raw, BK_GEOM_PLACE);
        BK_geom_set(Raw, "relx", Right ? 1.0 : 0.0);
        BK_geom_set(Raw, "rely", Bottom ? 1.0 : 0.0);
        BK_geom_set(Raw, "x", Right ? -Radius : 0.0);
        BK_geom_set(Raw, "y", Bottom ? -Radius : 0.0);
        BK_geom_set(Raw, "width", Radius);
        BK_geom_set(Raw, "height", Radius);
    }
}

} // namespace scriptroom::gui
