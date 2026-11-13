#pragma once

#include <functional>
#include <string_view>
#include <vector>

namespace scriptroom::gui {

/// 返回一段 UTF-8 文本渲染后的宽度（像素）。
using TextMeasure = std::function<float(std::string_view)>;

/// 把文本折成不超过 MaxWidth 的若干行（视图里的字符串视图，生命周期同 Text）。
///
/// - 显式的 \n（及其前的 \r）强制换行，空行保留；
/// - 优先在空格、路径分隔符和逗号之后换行，找不到合适位置才在字符边界硬折；
/// - 绝不拆开 UTF-8 多字节字符；每行至少容纳一个字符，所以窄宽度不会死循环。
std::vector<std::string_view> wrap_text(std::string_view Text, float MaxWidth, const TextMeasure &Measure);

} // namespace scriptroom::gui
