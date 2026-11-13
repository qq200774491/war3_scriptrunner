#pragma once

#include "PillButton.h"
#include "TextBlock.h"
#include "ViewServices.h"

#include <array>
#include <optional>

namespace scriptroom::gui {

class InspectorPanel {
public:
    InspectorPanel(const ViewServices &Context, Node Parent);

    Node root() const { return Body; }

    /// Resolved 为草稿当前解析出的配置；无效时隐藏路径与设置。
    void sync(const std::optional<Config> &Resolved);

private:
    void add_path(Node Parent, const char *Title, TextBlock &Value);

    const ViewServices &Services;
    Node Body;
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
