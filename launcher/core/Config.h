#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace scriptroom {

namespace fs = std::filesystem;

/// 本地测试配置：九个字段，未知字段一律拒绝。
struct Config {
    fs::path Map;
    fs::path LocalEntry;
    fs::path CloudScripts;
    fs::path Archives;
    fs::path Editor;
    fs::path SourceGame;
    fs::path Game;
    std::uint64_t Seconds = 3600;
    double StartupTimeout = 90.0;

    /// 读取并校验配置；相对路径相对于配置文件所在目录。
    static Config load(const fs::path &Path);

    /// 把相对路径解析到 Parent 下；任一路径为空时抛出 Error。
    void resolve_paths(const fs::path &Parent);

    /// 校验运行时长与启动超时的取值范围。
    void validate_numbers() const;

    /// 校验后原子写入 JSON。
    void save(const fs::path &Path) const;
};

/// 向上查找包含 scriptrunner.dll 与 map/legacy_mls.lua 的运行资源根目录。
fs::path find_root();

/// 默认配置文件位置。
fs::path default_config(const fs::path &Root);

} // namespace scriptroom
