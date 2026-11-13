#pragma once

#include "PillButton.h"
#include "TextBlock.h"
#include "ViewServices.h"

#include <array>
#include <optional>

namespace scriptroom::gui {

class ArchivesPage {
public:
    ArchivesPage(const ViewServices &Context, Node Parent);

    Node root() const { return Body; }

    /// Resolved 为草稿当前解析出的配置；草稿无效时为空，页面只显示标题。
    void sync(const std::optional<Config> &Resolved);

private:
    struct PlayerPaths {
        TextBlock ClientIni;
        TextBlock CloudArchive;
    };

    void build_player(Node Parent, std::size_t Player);

    const ViewServices &Services;
    Node Body;
    Node Details;
    Node Unresolved;
    PillButton OpenFolder;
    std::array<Node, MaxClients> Sections;
    std::array<PlayerPaths, MaxClients> Players;
};

} // namespace scriptroom::gui
