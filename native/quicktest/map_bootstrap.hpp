#pragma once
#include <cstdint>
#include <filesystem>

namespace quicktest {
class Session;

// 只覆盖受保护地图的配置阶段；离开作用域后所有创建请求照常执行。
class MapSetupScope final {
public:
    MapSetupScope() noexcept;
    ~MapSetupScope();
    MapSetupScope(const MapSetupScope&) = delete;
    MapSetupScope& operator=(const MapSetupScope&) = delete;
};

void install_map_bootstrap(std::uintptr_t game, const std::filesystem::path& map, const Session& session);
}
