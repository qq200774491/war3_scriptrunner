#include "GameMonitor.h"

#include "Error.h"
#include "FileSystem.h"
#include "Text.h"

#include <algorithm>
#include <format>

namespace scriptroom {

namespace {

using Millis = std::chrono::milliseconds;
constexpr Millis StartLimit{15000};
constexpr Millis DrainLimit{5000};

std::string helper_errors(const std::filesystem::path &Path) {
    try {
        return utf8_lossy(read_file(Path));
    } catch (const Error &) {
        return {};
    }
}

} // namespace

GameMonitor GameMonitor::start(const ChildSpec &Spec, const std::filesystem::path &ErrorLog) {
    ChildSpec Request = Spec;
    Request.PipeStdout = true;
    Request.KillOnDestroy = false;
    Request.StderrFile = ErrorLog;
    ChildProcess Helper = [&] {
        try {
            return ChildProcess::spawn(Request);
        } catch (const Error &Failure) {
            throw Error(std::format("创建游戏启动助手: {}", Failure.what()));
        }
    }();
    GameMonitor Monitor(std::move(Helper), ErrorLog);
    try {
        const auto Deadline = std::chrono::steady_clock::now() + StartLimit;
        while (true) {
            const Millis Left = std::chrono::duration_cast<Millis>(Deadline - std::chrono::steady_clock::now());
            require(Left.count() > 0, "游戏启动助手超时");
            const std::optional<std::string> Line = Monitor.read_line(Left);
            if (!Line) {
                throw Error(std::format("游戏启动失败：{}", helper_errors(ErrorLog)));
            }
            if (Line->starts_with("GAME_PID ")) {
                const std::optional<std::uint64_t> Value = parse_u64(trim(Line->substr(9)));
                require(Value && *Value <= 0xFFFFFFFFu, "非法游戏 PID");
                Monitor.Pid = static_cast<std::uint32_t>(*Value);
                break;
            }
        }
    } catch (...) {
        Monitor.Helper.kill();
        throw;
    }
    return Monitor;
}

std::optional<std::string> GameMonitor::read_line(Millis Timeout) {
    const auto Deadline = std::chrono::steady_clock::now() + Timeout;
    while (true) {
        const std::size_t Newline = Pending.find('\n');
        if (Newline != std::string::npos) {
            std::string Line = Pending.substr(0, Newline);
            Pending.erase(0, Newline + 1);
            if (!Line.empty() && Line.back() == '\r') {
                Line.pop_back();
            }
            return utf8_lossy(Line);
        }
        if (OutputClosed) {
            if (Pending.empty()) {
                return std::nullopt;
            }
            std::string Line = std::move(Pending);
            Pending.clear();
            return utf8_lossy(Line);
        }
        char Buffer[4096];
        std::size_t Got = 0;
        const Millis Left = std::max(Millis(0), std::chrono::duration_cast<Millis>(Deadline - std::chrono::steady_clock::now()));
        const IoStatus Status = Helper.stdout_pipe().read_some(Buffer, sizeof(Buffer), Got, Left);
        if (Status == IoStatus::Eof) {
            OutputClosed = true;
        } else if (Status == IoStatus::Timeout) {
            throw Error("读取启动助手输出超时");
        } else if (Status == IoStatus::Cancelled) {
            OutputClosed = true;
        } else {
            Pending.append(Buffer, Got);
        }
    }
}

std::optional<std::uint32_t> GameMonitor::exit_code() {
    if (Exit) {
        return Exit;
    }
    const std::optional<std::uint32_t> Status = Helper.try_wait();
    if (!Status) {
        return std::nullopt;
    }
    try {
        while (const std::optional<std::string> Line = read_line(DrainLimit)) {
            if (Line->starts_with("GAME_EXIT ")) {
                const std::optional<std::uint64_t> Value = parse_u64(trim(Line->substr(10)));
                require(Value && *Value <= 0xFFFFFFFFu, "非法游戏退出码");
                Exit = static_cast<std::uint32_t>(*Value);
            }
        }
    } catch (const Error &Failure) {
        // 游戏进程可能继承了助手的输出句柄而不关闭；超时后按已读到的内容判断。
        if (std::string_view(Failure.what()) != "读取启动助手输出超时") {
            throw;
        }
    }
    require(*Status == 0 && Exit.has_value(),
            std::format("游戏 PID {} 的监测助手异常结束（无法确认游戏退出码）：{}", Pid, helper_errors(ErrorLog)));
    return Exit;
}

void GameMonitor::detach() {
    Helper.detach();
}

} // namespace scriptroom
