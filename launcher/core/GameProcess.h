#pragma once

#include "Win32.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace scriptroom {

/// 运行中的魔兽进程。
struct GameProcess {
    std::uint32_t Pid = 0;
    std::string Name;
};

/// 枚举本机所有 war3.exe / Warcraft III.exe。枚举失败时抛出 Error。
std::vector<GameProcess> list_games();

/// 向进程的可见窗口发送正常关闭请求，返回给用户看的结果说明。
std::string request_close(const GameProcess &Game);

/// 命名事件是否已触发；事件不存在时返回 false。
bool event_signaled(std::string_view Name);

/// 强制结束某个魔兽进程所需的已验证句柄。
///
/// 优先使用启动助手持有的创建句柄（游戏收紧权限后外部无法直接结束它）；
/// 确认弹窗之前就捕获句柄，之后不再按 PID 重新定位，避免 PID 复用误杀其他进程。
class ForceTarget {
public:
    /// 核实进程名并取得结束权限；失败时抛出 Error。
    static ForceTarget capture(const GameProcess &Game);

    ForceTarget(ForceTarget &&) noexcept = default;
    ForceTarget &operator=(ForceTarget &&) noexcept = default;
    ForceTarget(const ForceTarget &) = delete;
    ForceTarget &operator=(const ForceTarget &) = delete;

    std::uint32_t pid() const { return Pid; }

    /// 是否走启动助手的控制通道（而非直接持有进程句柄）。
    bool uses_owner_control() const { return Exited.valid(); }

    /// 结束进程并等待至多 3 秒；已退出则直接报告。失败时抛出 Error。
    std::string terminate();

private:
    ForceTarget() = default;

    std::uint32_t Pid = 0;
    UniqueHandle Primary; ///< 直接路径：进程句柄；助手路径：终止请求事件
    UniqueHandle Exited;  ///< 助手路径：进程已退出事件
};

} // namespace scriptroom
