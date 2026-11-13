#pragma once
#include <cstdint>
#include <filesystem>
namespace quicktest {
class Session;
// 仅覆盖本进程的局域网地图读取，不改动用户注册表和其他游戏进程。
void install_map_selection(std::uintptr_t load_string, const std::filesystem::path& map, const Session& session);
}
