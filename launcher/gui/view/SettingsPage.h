#pragma once

#include "ConfigDraft.h"
#include "ViewServices.h"

#include <array>
#include <cstdint>

namespace scriptroom::gui {

/// 控件里的文字就是 AppModel::draft() 的镜像：用户编辑时逐键写回草稿，
/// 草稿被整体替换（载入配置）时由 sync 推回控件。
class SettingsPage {
public:
    SettingsPage(const ViewServices &Context, Node Parent);

    Node root() const { return Body; }
    void sync();

private:
    struct PathRow;

    void build_toolbar();
    Node group(Node Content) const;
    void build_path_row(Node Group, const PathRow &Spec);
    void build_limits(Node Content);
    void build_appearance(Node Content);
    void browse(PathField Field, const char *Title, bool Directory);
    void load_dialog();
    void save_as_dialog();
    void push_draft_to_controls();
    HWND owner() const;

    const ViewServices &Services;
    Node Body;
    std::array<Node, PathFieldCount> PathFields;
    Node SecondsField;
    Node TimeoutField;
    Node ThemeName;
    Node ConfigPathLabel;
    std::uint64_t SeenRevision = 0;
};

} // namespace scriptroom::gui
