#pragma once

#include <string_view>

namespace scriptroom::gui {

/// 日志行的来源列。
enum class LogSource { System, Script, Error };

/// 控制台的范围筛选。
enum class LogScope { All, Cloud, Errors };

/// 含“[云脚本” 的行来自云脚本（与 Rust 版一致，按包含判断；云脚本日志实际总以它开头）。
bool is_script_line(std::string_view Line);

/// 含“故障”或 error / failed（不分大小写）的行视为错误。
bool is_error_line(std::string_view Line);

/// 来源列：错误优先于脚本。
LogSource classify_line(std::string_view Line);

/// 来源列的显示文字：系统 / 脚本 / 错误。
const char *source_label(LogSource Source);

/// 范围筛选的显示文字：全部日志 / 云脚本 / 错误与异常。
const char *scope_label(LogScope Scope);

} // namespace scriptroom::gui
