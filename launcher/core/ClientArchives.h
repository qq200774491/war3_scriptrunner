#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace scriptroom {

namespace fs = std::filesystem;

/// 整理客户端 INI 存档，返回这一轮 Players 位玩家的 player-0.ini … player-<Players-1>.ini 路径。
///
/// 每位玩家只保留自己 SSV-<玩家号>-* 键的权威值，其余设置原样保留；
/// 旧版 v1 布局先备份再迁移；layout.json 记下用过存档的玩家，记过的玩家文件缺失时拒绝启动，
/// 绝不用队友的旧副本复活已删除的存档。人数变化不影响这条：每次都整理记过的玩家和这一轮的玩家，
/// 第一次出现的玩家从初始内容建档。任一 INI 正被运行中的游戏占用（独占锁失败）时整体失败。
/// @param Folder 存档的 clients 目录。
/// @param Players 这一轮的客户端个数。
/// @param Legacy 首次迁移时可用的旧版共用 INI；没有则为 nullopt。
std::vector<fs::path> prepare_client_archives(const fs::path &Folder, std::size_t Players,
                                              const std::optional<fs::path> &Legacy);

} // namespace scriptroom
