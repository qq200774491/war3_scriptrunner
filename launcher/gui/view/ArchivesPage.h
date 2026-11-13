#pragma once

#include "TextBlock.h"
#include "ViewServices.h"

#include <array>
#include <optional>

namespace scriptroom::gui {

/// 玩家存档页：两位玩家各自的客户端 INI 与云端存档路径，以及“打开存档目录”。
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
    Node Details; ///< 玩家卡片与按钮；草稿无效时隐藏
    std::array<PlayerPaths, 2> Players;
};

} // namespace scriptroom::gui
