#pragma once
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace room {
// 本地测试平台：槽位身份固定，存档目录由启动器显式指定，与正式平台存档隔离。
class LocalPlatform final {
public:
    LocalPlatform(const std::vector<int>& players, const std::filesystem::path& archive_directory);
    ~LocalPlatform();
    LocalPlatform(const LocalPlatform&) = delete;
    LocalPlatform& operator=(const LocalPlatform&) = delete;
    const char* player_name(int player) const;
    int start_time() const { return start_time_; }
    int game_time() const;
    int played_count(int player) const;
    const std::string* archive(int player) const;
    void save_archive(int player, const char* bytes, int length);
    void end(int player);

private:
    struct Player {
        std::string name;
        std::optional<std::string> archive;
        bool ended = false;
        int sessions = 0;
    };
    class ArchiveDirectory;
    std::unique_ptr<ArchiveDirectory> directory_;
    std::map<int, Player> players_;
    int start_time_;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
};
}
