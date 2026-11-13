#pragma once

#include "ChildProcess.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace scriptroom {

/// 游戏启动助手（lua_probe）的监测端。
///
/// 助手保留创建游戏时的进程句柄，即使游戏之后收紧权限也能读到退出码；
/// 它在标准输出打印 `GAME_PID <pid>`，游戏结束时打印 `GAME_EXIT <code>`。
class GameMonitor {
public:
    /// 启动助手并等待它报告游戏 PID（最多 15 秒）；失败时抛出带助手错误输出的 Error。
    static GameMonitor start(const ChildSpec &Helper, const std::filesystem::path &ErrorLog);

    GameMonitor(GameMonitor &&) noexcept = default;
    GameMonitor &operator=(GameMonitor &&) noexcept = default;
    GameMonitor(const GameMonitor &) = delete;
    GameMonitor &operator=(const GameMonitor &) = delete;

    std::uint32_t pid() const { return Pid; }

    /// 游戏仍在运行返回 nullopt；已退出返回退出码。
    /// 助手异常结束而无法确认退出码时抛出 Error，不会把缺失的汇报当成游戏退出。
    std::optional<std::uint32_t> exit_code();

    /// 游戏仍运行时保留独立助手及其创建句柄，停止云服务后依然能通过界面关闭游戏。
    void detach();

private:
    GameMonitor(ChildProcess Helper, std::filesystem::path ErrorLog)
        : Helper(std::move(Helper)), ErrorLog(std::move(ErrorLog)) {}

    /// 读一行标准输出（去掉换行）；到达 EOF 返回 nullopt，超时抛出 Error。
    std::optional<std::string> read_line(std::chrono::milliseconds Timeout);

    ChildProcess Helper;
    std::filesystem::path ErrorLog;
    std::string Pending;
    bool OutputClosed = false;
    std::uint32_t Pid = 0;
    std::optional<std::uint32_t> Exit;
};

} // namespace scriptroom
