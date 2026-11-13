#pragma once

#include "BuiNode.h"

#include <functional>

namespace scriptroom::gui {

/// 在 Root 的四个角各放一个小节点，把圆角外的像素淡成透明（帧缓冲要保留 alpha，见 WindowChrome）。
/// 节点按添加顺序绘制，所以要在其余内容之后调用，才能淡掉画在角上的所有内容。
/// Active 返回假时（例如最大化）保持直角。节点归 Root 所有。
void add_rounded_corners(Node Root, int Radius, std::function<bool()> Active);

} // namespace scriptroom::gui
