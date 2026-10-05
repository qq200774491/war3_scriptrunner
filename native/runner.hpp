#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace room {
// 所有 DLL 操作归同一线程；未知 NativeInterface 回调终止隔离进程。
class Runner final {
public:
    Runner(const std::filesystem::path& dll, const std::filesystem::path& scripts,
           const std::vector<int>& players, const std::filesystem::path& archives = {});
    ~Runner();
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;
    void event(int player, const std::string& name, const std::string& payload);
    void tick();
    void close_room();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
