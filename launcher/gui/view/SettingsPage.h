#pragma once

#include "ConfigDraft.h"
#include "ViewServices.h"

#include <array>
#include <cstdint>

namespace scriptroom::gui {

/// 路径与配置页：七个路径、服务时长与启动超时的表单，以及保存 / 载入 / 另存为。
///
/// 控件里的文字就是 AppModel::draft() 的镜像：用户编辑时逐键写回草稿，
/// 草稿被整体替换（载入配置）时由 sync 推回控件。
class SettingsPage {
public:
    SettingsPage(const ViewServices &Context, Node Parent);

    Node root() const { return Body; }
    void sync();

private:
    struct PathRow;

    void build_path_row(Node Card, const PathRow &Spec, bool Last);
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
    Node ConfigPathLabel;
    std::uint64_t SeenRevision = 0;
};

} // namespace scriptroom::gui
