#pragma once

#include <array>
#include <filesystem>
#include <optional>

namespace scriptroom {

namespace fs = std::filesystem;

/// 整理两位玩家的客户端 INI 存档并返回 player-0.ini、player-1.ini 路径。
///
/// 每位玩家只保留自己 SSV-<玩家号>-* 键的权威值，其余设置原样保留；
/// 旧版 v1 布局先备份再迁移；已迁移后文件缺失时拒绝启动，绝不用队友的旧副本复活已删除的存档。
/// 任一 INI 正被运行中的游戏占用（独占锁失败）时整体失败。
/// @param Folder 存档的 clients 目录。
/// @param Legacy 首次迁移时可用的旧版共用 INI；没有则为 nullopt。
std::array<fs::path, 2> prepare_client_archives(const fs::path &Folder,
                                                const std::optional<fs::path> &Legacy);

} // namespace scriptroom
