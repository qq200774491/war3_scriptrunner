#include "GameProcess.h"

#include "Error.h"
#include "Text.h"

#include <format>
#include <tlhelp32.h>

namespace scriptroom {

namespace {

bool is_game_name(std::string_view Name) {
    return equals_ignore_case(Name, "war3.exe") || equals_ignore_case(Name, "warcraft iii.exe");
}

struct CloseState {
    DWORD Pid;
    int Sent;
    int Failed;
};

BOOL CALLBACK close_window(HWND Window, LPARAM Context) {
    auto &State = *reinterpret_cast<CloseState *>(Context);
    DWORD Owner = 0;
    GetWindowThreadProcessId(Window, &Owner);
    if (Owner == State.Pid && IsWindowVisible(Window)) {
        if (PostMessageW(Window, WM_CLOSE, 0, 0)) {
            ++State.Sent;
        } else {
            ++State.Failed;
        }
    }
    return TRUE;
}

} // namespace

std::vector<GameProcess> list_games() {
    UniqueHandle Snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    require(Snapshot.valid(), "无法枚举魔兽进程：" + last_error_text());
    PROCESSENTRY32W Entry{};
    Entry.dwSize = sizeof(Entry);
    std::vector<GameProcess> Result;
    BOOL Found = Process32FirstW(Snapshot.get(), &Entry);
    while (Found) {
        const std::string Name = utf8_lossy(to_utf8(Entry.szExeFile));
        if (is_game_name(Name)) {
            Result.push_back({Entry.th32ProcessID, Name});
        }
        Found = Process32NextW(Snapshot.get(), &Entry);
    }
    const DWORD Code = GetLastError();
    require(Code == ERROR_NO_MORE_FILES, std::format("枚举进程失败：{}", Code));
    return Result;
}

std::string request_close(const GameProcess &Game) {
    CloseState State{Game.Pid, 0, 0};
    require(EnumWindows(close_window, reinterpret_cast<LPARAM>(&State)) != FALSE, "无法枚举游戏窗口");
    return std::format("PID {}：已发送 {} 个正常关闭请求，{} 个失败{}", Game.Pid, State.Sent, State.Failed,
                       State.Sent == 0 ? "（窗口不可见或已退出）" : "；等待游戏退出");
}

bool event_signaled(std::string_view Name) {
    const std::wstring Wide = to_wide(Name);
    UniqueHandle Event(OpenEventW(SYNCHRONIZE, FALSE, Wide.c_str()));
    if (!Event.valid()) {
        const DWORD Code = GetLastError();
        if (Code == ERROR_FILE_NOT_FOUND) {
            return false;
        }
        throw Error(system_error_text(Code));
    }
    switch (WaitForSingleObject(Event.get(), 0)) {
    case WAIT_OBJECT_0:
        return true;
    case WAIT_TIMEOUT:
        return false;
    default:
        throw Error(last_error_text());
    }
}

ForceTarget ForceTarget::capture(const GameProcess &Game) {
    require(is_game_name(Game.Name), "不是魔兽进程");
    ForceTarget Target;
    Target.Pid = Game.Pid;

    const std::wstring Prefix = std::format(L"Local\\ScriptRoomGame-{}", Game.Pid);
    UniqueHandle Request(OpenEventW(EVENT_MODIFY_STATE, FALSE, (Prefix + L"-terminate").c_str()));
    if (Request.valid()) {
        UniqueHandle Exited(OpenEventW(SYNCHRONIZE, FALSE, (Prefix + L"-exited").c_str()));
        require(Exited.valid(), std::format("PID {} 的启动助手控制通道不完整：{}", Game.Pid, last_error_text()));
        Target.Primary = std::move(Request);
        Target.Exited = std::move(Exited);
        return Target;
    }

    UniqueHandle Process(OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                     Game.Pid));
    require(Process.valid(), std::format("PID {} 无法取得结束权限：{}", Game.Pid, last_error_text()));
    std::wstring Buffer(32768, L'\0');
    DWORD Size = static_cast<DWORD>(Buffer.size());
    require(QueryFullProcessImageNameW(Process.get(), 0, Buffer.data(), &Size) != FALSE, "无法核实进程身份");
    Buffer.resize(Size);
    const std::string Name = utf8_lossy(to_utf8(std::filesystem::path(Buffer).filename().native()));
    require(!Name.empty(), "无进程名称");
    require(equals_ignore_case(Name, Game.Name), "进程身份已改变");
    Target.Primary = std::move(Process);
    return Target;
}

std::string ForceTarget::terminate() {
    const HANDLE Wait = uses_owner_control() ? Exited.get() : Primary.get();
    if (WaitForSingleObject(Wait, 0) == WAIT_OBJECT_0) {
        return std::format("PID {} 已退出", Pid);
    }
    const BOOL Sent = uses_owner_control() ? SetEvent(Primary.get()) : TerminateProcess(Primary.get(), 1);
    require(Sent != FALSE, std::format("PID {} 结束请求失败：{}", Pid, last_error_text()));
    require(WaitForSingleObject(Wait, 3000) == WAIT_OBJECT_0, std::format("PID {} 尚未退出，未报告关闭成功", Pid));
    return std::format("PID {} 已强制结束", Pid);
}

} // namespace scriptroom
