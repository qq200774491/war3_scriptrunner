#include "local_platform.hpp"
#include <Windows.h>
#include <array>
#include <charconv>
#include <ctime>
#include <limits>
#include <stdexcept>

namespace room {
namespace {
constexpr DWORD max_archive_bytes = 4 * 1024 * 1024;

class File final {
public:
    explicit File(HANDLE handle) : handle_(handle) {
        if (handle == INVALID_HANDLE_VALUE)
            throw std::runtime_error("archive file open failed: " + std::to_string(GetLastError()));
    }
    ~File() { CloseHandle(handle_); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    HANDLE get() const { return handle_; }
private:
    HANDLE handle_;
};

std::optional<std::string> read_archive(const std::filesystem::path& path) {
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND) return std::nullopt;
    File file(handle);
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file.get(), &length) || length.QuadPart > max_archive_bytes)
        throw std::runtime_error("invalid local archive size");
    std::string data(static_cast<std::size_t>(length.QuadPart), '\0');
    DWORD received = 0;
    if (!ReadFile(file.get(), data.data(), static_cast<DWORD>(data.size()), &received, nullptr) || received != data.size())
        throw std::runtime_error("local archive read failed");
    return data;
}
}

class LocalPlatform::ArchiveDirectory final {
public:
    explicit ArchiveDirectory(const std::filesystem::path& root)
        : root_(prepare(root)), lock_(CreateFileW((root_ / L".room.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)) {}

    std::optional<std::string> read(int player) const { return read_archive(path(player)); }
    void write(int player, const std::string& value) const {
        write_file(path(player), value);
    }
    int begin_session(int player) const {
        const auto target = root_ / ("slot-" + std::to_string(player) + ".sessions");
        const auto previous = read_archive(target);
        int sessions = 0;
        if (previous) {
            const auto parsed = std::from_chars(previous->data(), previous->data() + previous->size(), sessions);
            if (parsed.ec != std::errc{} || parsed.ptr != previous->data() + previous->size() || sessions < 0)
                throw std::runtime_error("invalid local session count");
        }
        if (sessions == std::numeric_limits<int>::max()) throw std::runtime_error("local session count exhausted");
        write_file(target, std::to_string(++sessions));
        return sessions;
    }

private:
    static void write_file(const std::filesystem::path& target, const std::string& value) {
        const auto pending = std::filesystem::path(target.wstring() + L".pending");
        {
            File file(CreateFileW(pending.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr));
            DWORD written = 0;
            if (!WriteFile(file.get(), value.data(), static_cast<DWORD>(value.size()), &written, nullptr) ||
                written != value.size() || !FlushFileBuffers(file.get()))
                throw std::runtime_error("local archive write failed; old archive retained");
        }
        if (!MoveFileExW(pending.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("local archive replace failed; old archive retained");
    }

    static std::filesystem::path prepare(const std::filesystem::path& root) {
        auto result = std::filesystem::absolute(root);
        std::filesystem::create_directories(result);
        return result;
    }
    std::filesystem::path path(int player) const { return root_ / ("slot-" + std::to_string(player) + ".archive"); }
    std::filesystem::path root_;
    File lock_;
};

LocalPlatform::LocalPlatform(const std::vector<int>& players, const std::filesystem::path& archive_directory)
    : start_time_(static_cast<int>(std::time(nullptr))) {
    if (!archive_directory.empty()) directory_ = std::make_unique<ArchiveDirectory>(archive_directory);
    for (int player : players) {
        players_.emplace(player, Player{"SRPlayer" + std::to_string(player),
            directory_ ? directory_->read(player) : std::nullopt, false,
            directory_ ? directory_->begin_session(player) : 0});
    }
}
LocalPlatform::~LocalPlatform() = default;

const char* LocalPlatform::player_name(int player) const {
    if (player == -1) return "LocalSystem";
    return players_.at(player).name.c_str();
}
int LocalPlatform::game_time() const {
    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - started_).count());
}
int LocalPlatform::played_count(int player) const {
    if (!directory_) throw std::runtime_error("local session count requires archive directory");
    return players_.at(player).sessions;
}
const std::string* LocalPlatform::archive(int player) const {
    if (!directory_) throw std::runtime_error("local archive directory was not configured");
    const auto& value = players_.at(player).archive;
    return value ? &*value : nullptr;
}
void LocalPlatform::save_archive(int player, const char* bytes, int length) {
    if (!directory_) throw std::runtime_error("local archive directory was not configured");
    if (!bytes || length < 0 || static_cast<unsigned>(length) > max_archive_bytes)
        throw std::runtime_error("invalid local archive data");
    auto& target = players_.at(player);
    if (target.ended) throw std::runtime_error("cannot save an ended player");
    std::string value(bytes, static_cast<std::size_t>(length));
    directory_->write(player, value);
    target.archive = std::move(value);
}
void LocalPlatform::end(int player) {
    if (player == -1) {
        for (auto& entry : players_) entry.second.ended = true;
    } else {
        players_.at(player).ended = true;
    }
}
}
