#pragma once

#include "TextBlock.h"
#include "ViewServices.h"

#include <array>
#include <optional>

namespace scriptroom::gui {

/// 右侧详情栏：选中日志全文、运行路径、存档目录与运行设置。
class InspectorPanel {
public:
    InspectorPanel(const ViewServices &Context, Node Parent);

    Node root() const { return Body; }

    /// Resolved 为草稿当前解析出的配置；无效时隐藏路径与设置。
    void sync(const std::optional<Config> &Resolved);

private:
    /// 加一项“小标题 + 自动折行的路径”。
    void add_path(Node Parent, const char *Title, TextBlock &Value);

    const ViewServices &Services;
    Node Body;
    Node StatusText;
    Node SelectedSection;
    Node ConfigSection;
    Node DurationValue;
    Node TimeoutValue;
    TextBlock SelectedText;
    TextBlock MapPath;
    TextBlock ScriptsPath;
    TextBlock EntryPath;
    TextBlock ArchivesPath;
};

} // namespace scriptroom::gui
