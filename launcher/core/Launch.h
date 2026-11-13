#pragma once

#include "Config.h"
#include "Room.h"
#include "RunStats.h"
#include "Sync.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace scriptroom {

namespace fs = std::filesystem;

/// 双人开局所处的阶段。
enum class LaunchPhase {
    WaitingForLan, ///< 房主尚未创建局域网房间
    LanStarted,    ///< 已创建，之后不再回退
};

/// 阶段只由房主发出的信号推进，不依赖日志文本，也不依赖信号对象的存活时间。
void observe_phase(LaunchPhase &Phase, bool Started);
const char *phase_name(LaunchPhase Phase);

/// 把文本编码成 Lua 5.1 字符串字面量：纯 ASCII 只转义必要字符，
/// 含非 ASCII 时全部按十进制字节转义（\ddd），不使用 \u。
std::string lua_literal(std::string_view Text);

/// 检查配置文件、原生组件、魔兽文件、编辑器插件和 Game.dll 版本；失败时抛出 Error。
void check_setup(const fs::path &Root, const Config &Settings);

/// 完整的一轮双人测试：整理存档与目录、起房间与云脚本、依次启动两个客户端并监测，
/// 直到收到停止信号、时限到或出错。游戏窗口在服务结束后继续运行。
/// 日志写入 Output（同时落盘到 build/game）；出错时抛出 Error。
/// Stats 在云服务启动时清零并开始计时，停止后保留这一轮的数据；可为空。
void run_session(const fs::path &Root, const Config &Settings, StopHandle Stop, std::shared_ptr<LogChannel> Output,
                 std::shared_ptr<RunStats> Stats = nullptr);

} // namespace scriptroom
