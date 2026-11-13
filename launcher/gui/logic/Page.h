#pragma once

namespace scriptroom::gui {

/// 主工作区的页面。
enum class Page { Console, Stats, Settings, Archives, About };

/// 命令行 --page 与截图元数据使用的名字：console / stats / settings / archives / about。
inline const char *page_key(Page Target) {
    switch (Target) {
    case Page::Stats:
        return "stats";
    case Page::Settings:
        return "settings";
    case Page::Archives:
        return "archives";
    case Page::About:
        return "about";
    case Page::Console:
        break;
    }
    return "console";
}

} // namespace scriptroom::gui
