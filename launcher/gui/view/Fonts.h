#pragma once

namespace scriptroom::gui {

/// 让 BUI 的默认界面字体换成系统中文字体。
///
/// BUI 内置的 Inter 不含汉字，且目前没有逐字形回退，缺字会显示成方框，
/// 所以必须在创建任何节点或窗口之前调用。常规体按顺序尝试微软雅黑、黑体、宋体，
/// 粗体用微软雅黑粗体（TTC 取第 0 个字体）；字体以 family 为空注册，即成为默认族。
/// 没有常规体可用时返回 false，界面仍可运行，但汉字会显示为方框。
bool use_system_cjk_font();

} // namespace scriptroom::gui
