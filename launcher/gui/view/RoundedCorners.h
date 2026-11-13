#pragma once

#include "Surface.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace scriptroom::gui {

/// 在 Root 的四个角和四条边上放最后绘制的小节点：角上把圆弧外的像素淡成透明，并沿四边和圆弧画 1px 描边
/// （帧缓冲要保留 alpha，见 WindowChrome）。节点按添加顺序绘制，所以要在其余内容之后调用。
/// Active 返回假时（例如最大化）保持直角、不画描边；Outline 返回描边色（0x00RRGGBB）。
/// 返回这些节点，换描边色时用来重画；节点归 Root 所有。
std::vector<Surface *> add_rounded_corners(Node Root, int Radius, std::function<bool()> Active,
                                           std::function<std::uint32_t()> Outline);

} // namespace scriptroom::gui
