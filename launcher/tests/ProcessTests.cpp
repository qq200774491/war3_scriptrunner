// 进程相关测试：游戏监测、强制结束路径、命名事件。

#include "ChildProcess.h"
#include "Config.h"
#include "Error.h"
#include "FileSystem.h"
#include "GameMonitor.h"
#include "GameProcess.h"
#include "TestHarness.h"
#include "Text.h"

#include <cstdio>
#include <format>
#include <thread>

namespace {

using namespace scriptroom;
using namespace testing;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

fs::path test_executable() {
    std::wstring Buffer(32768, L'\0');
    const DWORD Length = GetModuleFileNameW(nullptr, Buffer.data(), static_cast<DWORD>(Buffer.size()));
    Buffer.resize(Length);
    return fs::path(Buffer);
}

/// 用 cmd.exe 模拟启动助手：执行一段批处理命令并保留标准错误到日志。
GameMonitor start_fake_helper(const std::string &Script, const fs::path &ErrorLog) {
    ChildSpec Spec;
    Spec.Executable = L"C:\\Windows\\System32\\cmd.exe";
    Spec.Arguments = {"/D", "/C", Script};
    return GameMonitor::start(Spec, ErrorLog);
}

const Registrar MonitorCapturesExit("game_monitor.captures_game_exit_even_when_helper_succeeds", [] {
    TempDirectory Directory;
    GameMonitor Monitor = start_fake_helper("echo GAME_PID 123&echo GAME_EXIT 3221226505", Directory.path() / L"helper.log");
    check_equal(Monitor.pid(), std::uint32_t(123));
    std::optional<std::uint32_t> Code;
    for (int Attempt = 0; Attempt < 100 && !Code; ++Attempt) {
        Code = Monitor.exit_code();
        if (!Code) {
            std::this_thread::sleep_for(50ms);
        }
    }
    check_equal(Code.value_or(0), std::uint32_t(0xC0000409));
    check_equal(Monitor.exit_code().value_or(0), std::uint32_t(0xC0000409)); // 重复读取结果不变
    Monitor.detach();
});

const Registrar MonitorReportsStartupFailure("game_monitor.startup_failure_reports_helper_error", [] {
    TempDirectory Directory;
    check_throws([&] { start_fake_helper("echo expected launch failure 1>&2&exit /b 9", Directory.path() / L"helper.log"); },
                 "expected launch failure");
});

const Registrar MonitorRejectsMissingExit("game_monitor.missing_exit_report_is_not_claimed_as_game_exit", [] {
    TempDirectory Directory;
    GameMonitor Monitor = start_fake_helper("echo GAME_PID 456", Directory.path() / L"helper.log");
    for (int Attempt = 0; Attempt < 100; ++Attempt) {
        try {
            if (Monitor.exit_code()) {
                throw Failure("缺少汇报时不应得到退出码");
            }
        } catch (const Failure &) {
            throw;
        } catch (const Error &Reason) {
            check(std::string_view(Reason.what()).find("无法确认游戏退出码") != std::string_view::npos);
            Monitor.detach();
            return;
        }
        std::this_thread::sleep_for(50ms);
    }
    throw Failure("助手结束后应报告无法确认退出码");
});

/// 启动测试程序自身作为“魔兽”夹具，改名为 war3.exe。
ChildProcess spawn_fixture(const fs::path &Directory, const fs::path &ReadyFile) {
    const fs::path Executable = Directory / L"war3.exe";
    fs::copy_file(test_executable(), Executable, fs::copy_options::overwrite_existing);
    ChildSpec Spec;
    Spec.Executable = Executable;
    Spec.Arguments = {"--fixture-wait"};
    if (!ReadyFile.empty()) {
        Spec.Arguments.push_back(path_to_utf8(ReadyFile));
    }
    Spec.KillOnDestroy = true;
    return ChildProcess::spawn(Spec);
}

const Registrar ForceCloseChecksIdentity("game_process.process_identity_and_force_close", [] {
    TempDirectory Directory;
    ChildProcess Fixture = spawn_fixture(Directory.path(), {});
    const GameProcess Game{Fixture.pid(), "war3.exe"};
    bool Listed = false;
    for (const GameProcess &Running : list_games()) {
        Listed = Listed || Running.Pid == Game.Pid;
    }
    check(Listed, "夹具进程应出现在魔兽进程列表中");
    check_throws([&] { ForceTarget::capture(GameProcess{Game.Pid, "other.exe"}); }, {});
    ForceTarget Target = ForceTarget::capture(Game);
    check(!Target.uses_owner_control());
    check(request_close(Game).find("0 个正常关闭") != std::string::npos);
    check(Target.terminate().find("已强制结束") != std::string::npos);
    check(Fixture.wait_for(3s).has_value());
});

const Registrar EventSignalReportsMissing("game_process.missing_event_is_not_signaled", [] {
    check(!event_signaled("Local\\ScriptRoomTest-no-such-event"));
});

const Registrar OwnerControlClosesAfterAccessDenied("game_process.owner_control_closes_process_after_external_access_is_denied", [] {
    const fs::path Root = project_root();
    const fs::path ConfigPath = default_config(Root);
    if (!fs::exists(ConfigPath)) {
        throw Skipped("缺少本地测试配置.json");
    }
    const Config Settings = Config::load(ConfigPath);
    const fs::path Probe = Root / L"build" / L"Release" / L"lua_probe.exe";
    const fs::path Monitor = Root / L"tools" / L"game_control.lua";
    if (!fs::exists(Probe) || !fs::exists(Settings.Editor / L"bin" / L"luacore.dll") || !fs::exists(Monitor)) {
        throw Skipped("缺少 lua_probe 或 KKWE 编辑器");
    }

    TempDirectory Directory;
    const fs::path Ready = Directory.path() / L"ready.txt";
    const fs::path Executable = Directory.path() / L"war3.exe";
    fs::copy_file(test_executable(), Executable);
    const std::string Command = std::format("\"{}\" --fixture-wait \"{}\"", path_to_utf8(Executable), path_to_utf8(Ready));
    const fs::path Script = Directory.path() / L"control.lua";
    atomic_write(Script, std::format(R"(
package.cpath = [==[{}/?.dll]==] .. ';' .. package.cpath
require 'filesystem'
require 'sys'
local process = sys.process()
assert(process:create(nil, [==[{}]==], fs.path([==[{}]==])))
local monitor = assert(loadfile([==[{}]==]))()
local code = monitor(process)
process:close()
print('GAME_EXIT ' .. tostring(code))
)", path_to_utf8(Settings.Editor / L"bin" / L"modules"), Command, path_to_utf8(Directory.path()), path_to_utf8(Monitor)));

    ChildSpec Helper;
    Helper.Executable = Probe;
    Helper.Arguments = {path_to_utf8(Settings.Editor / L"bin" / L"luacore.dll"), path_to_utf8(Script)};
    Helper.PipeStdout = true;
    Helper.StderrFile = Directory.path() / L"helper.err";
    Helper.KillOnDestroy = true;
    Helper.Environment = {{"PATH", path_to_utf8(Settings.Editor / L"bin") + ";" + [] {
                              std::wstring Value(32768, L'\0');
                              Value.resize(GetEnvironmentVariableW(L"PATH", Value.data(), static_cast<DWORD>(Value.size())));
                              return to_utf8(Value);
                          }()}};
    ChildProcess Owner = ChildProcess::spawn(Helper);

    const auto Deadline = std::chrono::steady_clock::now() + 10s;
    while (!fs::is_regular_file(Ready)) {
        check(std::chrono::steady_clock::now() < Deadline, "夹具就绪超时");
        check(!Owner.try_wait().has_value(), "控制助手在夹具就绪前退出");
        std::this_thread::sleep_for(20ms);
    }
    const GameProcess Game{static_cast<std::uint32_t>(parse_u64(trim(read_file(Ready))).value()), "war3.exe"};

    // 夹具已拒绝外部访问：普通令牌直接打开进程会失败（错误码 5），只能走助手的控制通道。
    // 启用了 SeDebugPrivilege 的提权会话会绕过 DACL，此时无法复现该前提；
    // 下面对控制通道本身的验证与前提无关，照常进行。
    HANDLE Direct = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Game.Pid);
    if (Direct != nullptr) {
        CloseHandle(Direct);
        std::printf("  提示：当前令牌可绕过进程 DACL（SeDebugPrivilege），未复现错误 5，仅验证控制通道\n");
    } else {
        check_equal(GetLastError(), DWORD(5));
    }

    {
        ForceTarget Cancelled = ForceTarget::capture(Game);
        check(Cancelled.uses_owner_control());
    } // 捕获后取消不得结束游戏
    std::this_thread::sleep_for(150ms);
    check(!Owner.try_wait().has_value(), "捕获后取消不得结束游戏");

    ForceTarget Target = ForceTarget::capture(Game);
    ForceTarget Stale = ForceTarget::capture(Game);
    std::string Closed;
    std::thread([&] { Closed = Target.terminate(); }).join();
    check(Closed.find("已强制结束") != std::string::npos, Closed);
    check(Stale.terminate().find("已退出") != std::string::npos, "旧确认应保持绑定到已退出的进程");
    check(Owner.wait_for(10s).has_value());
});

} // namespace
