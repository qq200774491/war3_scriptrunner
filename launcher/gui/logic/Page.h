#pragma once

namespace scriptroom::gui {

/// 主工作区的三个页面。
enum class Page { Console, Settings, Archives };

/// 命令行 --page 与截图元数据使用的名字：console / settings / archives。
inline const char *page_key(Page Target) {
    switch (Target) {
    case Page::Settings:
        return "settings";
    case Page::Archives:
        return "archives";
    case Page::Console:
        break;
    }
    return "console";
}

} // namespace scriptroom::gui
