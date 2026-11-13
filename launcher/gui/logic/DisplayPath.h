#pragma once

#include <string>
#include <string_view>

namespace scriptroom::gui {

/// 界面上显示路径时统一用 Windows 的反斜杠，配置里手写的 D:/Code/x 和程序拼出来的 F:\code\y 看起来就一样了。
/// 只用于显示：配置文件里存的、交给核心的仍是原来的字符串。UNC 路径（\\server\share）保持原样。
std::string display_path(std::string_view Path);

/// 输入框显示的是 display_path(Original)。用户没改动（Shown 仍等于它）时返回原文，
/// 这样保存配置不会悄悄改掉已有的写法；改了才返回新输入。
std::string edited_path(std::string_view Shown, std::string_view Original);

} // namespace scriptroom::gui
