#pragma once

#include <string_view>

namespace scriptroom::gui {

/// 日志行的来源列。
enum class LogSource { System, Script, Error, Slow };

/// 控制台的范围筛选。
enum class LogScope { All, Cloud, Errors, Slow };

/// 含“[云脚本” 的行来自云脚本（与 Rust 版一致，按包含判断；云脚本日志实际总以它开头）。
bool is_script_line(std::string_view Line);

/// 含“故障”或 error / failed（不分大小写）的行视为错误。
bool is_error_line(std::string_view Line);

/// 房间写出的慢请求行（以“[慢请求]”开头）。它自成一类，不算错误，即使请求名里带 error。
bool is_slow_line(std::string_view Line);

/// 来源列：慢请求优先，其次错误，再次脚本。
LogSource classify_line(std::string_view Line);

/// 来源列的显示文字：系统 / 脚本 / 错误 / 慢请求。
const char *source_label(LogSource Source);

/// 范围筛选的显示文字：全部日志 / 云脚本 / 错误与异常 / 慢请求。
const char *scope_label(LogScope Scope);

} // namespace scriptroom::gui
