#pragma once

#include "Page.h"
#include "Theme.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace scriptroom::gui {

namespace fs = std::filesystem;

inline constexpr int DefaultWindowWidth = 1240;
inline constexpr int DefaultWindowHeight = 780;

/// --capture：不打开窗口，渲染一帧写成 PNG 后退出（开发验收用）。
struct CaptureRequest {
    fs::path Output;
    ThemeKind Theme = ThemeKind::Blue;
    Page Start = Page::Console;
    std::optional<fs::path> LogFile;
};

struct LaunchOptions {
    bool AutoStart = false; ///< --start：打开后自动执行一次“启动”
    int Width = DefaultWindowWidth;
    int Height = DefaultWindowHeight;
    std::optional<CaptureRequest> Capture;
};

/// 解析命令行（Tokens 不含程序名，均为 UTF-8）。
/// 与 Rust 版一致地宽松：带值选项缺值或取值无法识别时回落到默认值；截图模式下忽略 --start。
LaunchOptions parse_launch_options(const std::vector<std::string> &Tokens);

} // namespace scriptroom::gui
