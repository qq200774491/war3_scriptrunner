// ScriptRoomCLI：原生双人启动与共享云脚本的命令行入口。

#include "Config.h"
#include "Error.h"
#include "FileSystem.h"
#include "Host.h"
#include "Json.h"
#include "Launch.h"
#include "Room.h"
#include "Sync.h"
#include "Text.h"

#include <atomic>
#include <cstdio>
#include <format>
#include <map>
#include <optional>
#include <thread>
#include <vector>

#include <windows.h>
#include <shellapi.h>

namespace {

using namespace scriptroom;

constexpr const char *Version = "0.3.0";

/// 命令行选项。带值选项只接受 `--name value` 或 `--name=value`。
struct Arguments {
    std::optional<fs::path> ConfigPath;
    bool Check = false;
    std::optional<std::uint64_t> Seconds;
    std::optional<fs::path> StopFile;
    std::optional<fs::path> Serve; ///< 仅启动云服务，把两名玩家的连接参数写入该 JSON（集成测试使用）
    std::optional<fs::path> Scripts;
    std::optional<fs::path> Archives;
    bool Direct = false;
    std::optional<fs::path> Map;
    std::optional<fs::path> LocalEntry;
    std::optional<fs::path> CloudScripts;
    std::optional<fs::path> Editor;
    std::optional<fs::path> SourceGame;
    std::optional<fs::path> Game;
    std::optional<double> StartupTimeout;
    bool ShowHelp = false;
    bool ShowVersion = false;
};

void print_help() {
    std::puts(
        "ScriptRoom：原生双人启动与共享云脚本\n"
        "\n"
        "用法：ScriptRoomCLI [选项]\n"
        "\n"
        "选项：\n"
        "  --config <路径>          配置文件，默认程序旁的 本地测试配置.json\n"
        "  --check                  只检查配置与组件，不启动\n"
        "  --seconds <秒>           覆盖服务时长\n"
        "  --stop-file <路径>       该文件出现时停止服务\n"
        "  --startup-timeout <秒>   覆盖启动超时\n"
        "  --map / --local-entry / --cloud-scripts / --archives / --editor / --source-game / --game <路径>\n"
        "                           覆盖配置中的对应路径\n"
        "  --serve <JSON路径>       仅启动云服务并把玩家连接参数写入该文件（需 --scripts、--archives）\n"
        "  --scripts <目录>         --serve 使用的云脚本目录\n"
        "  --direct                 --serve 时按事件目标分发回调（默认全部转给玩家 0）\n"
        "  -h, --help               显示帮助\n"
        "  -V, --version            显示版本");
}

/// 解析命令行；非法选项抛出 Error。
Arguments parse_arguments(const std::vector<std::string> &Tokens) {
    Arguments Result;
    for (std::size_t Index = 0; Index < Tokens.size(); ++Index) {
        std::string Name = Tokens[Index];
        std::optional<std::string> Inline;
        if (Name.starts_with("--")) {
            if (const std::size_t Equals = Name.find('='); Equals != std::string::npos) {
                Inline = Name.substr(Equals + 1);
                Name.erase(Equals);
            }
        }
        const auto value = [&]() -> std::string {
            if (Inline) {
                return *Inline;
            }
            require(Index + 1 < Tokens.size(), std::format("选项 {} 需要一个值", Name));
            return Tokens[++Index];
        };
        const auto path_value = [&]() { return path_from_utf8(value()); };
        const auto flag = [&](bool &Target) {
            require(!Inline, std::format("选项 {} 不接受值", Name));
            Target = true;
        };

        if (Name == "--config") {
            Result.ConfigPath = path_value();
        } else if (Name == "--check") {
            flag(Result.Check);
        } else if (Name == "--seconds") {
            const std::optional<std::uint64_t> Seconds = parse_u64(value());
            require(Seconds.has_value(), "--seconds 需要非负整数");
            Result.Seconds = Seconds;
        } else if (Name == "--stop-file") {
            Result.StopFile = path_value();
        } else if (Name == "--serve") {
            Result.Serve = path_value();
        } else if (Name == "--scripts") {
            Result.Scripts = path_value();
        } else if (Name == "--archives") {
            Result.Archives = path_value();
        } else if (Name == "--direct") {
            flag(Result.Direct);
        } else if (Name == "--map") {
            Result.Map = path_value();
        } else if (Name == "--local-entry") {
            Result.LocalEntry = path_value();
        } else if (Name == "--cloud-scripts") {
            Result.CloudScripts = path_value();
        } else if (Name == "--editor") {
            Result.Editor = path_value();
        } else if (Name == "--source-game") {
            Result.SourceGame = path_value();
        } else if (Name == "--game") {
            Result.Game = path_value();
        } else if (Name == "--startup-timeout") {
            const std::optional<double> Timeout = parse_double(value());
            require(Timeout.has_value(), "--startup-timeout 需要数字");
            Result.StartupTimeout = Timeout;
        } else if (Name == "-h" || Name == "--help") {
            flag(Result.ShowHelp);
        } else if (Name == "-V" || Name == "--version") {
            flag(Result.ShowVersion);
        } else {
            throw Error(std::format("未知参数：{}", Tokens[Index]));
        }
    }
    return Result;
}

std::vector<std::string> command_line_tokens() {
    int Count = 0;
    wchar_t **Wide = CommandLineToArgvW(GetCommandLineW(), &Count);
    std::vector<std::string> Tokens;
    for (int Index = 1; Index < Count; ++Index) {
        Tokens.push_back(to_utf8(Wide[Index]));
    }
    LocalFree(Wide);
    return Tokens;
}

/// 停止来源：Ctrl+C 与 stop 文件。
StopHandle GlobalStop = std::make_shared<StopSignal>();

BOOL WINAPI on_console_event(DWORD Kind) {
    if (Kind == CTRL_C_EVENT || Kind == CTRL_BREAK_EVENT || Kind == CTRL_CLOSE_EVENT) {
        GlobalStop->request();
        return TRUE;
    }
    return FALSE;
}

/// 把日志队列逐行打印到标准输出。
std::thread start_printer(std::shared_ptr<LogChannel> Logs) {
    return std::thread([Logs] {
        while (std::optional<std::string> Line = Logs->recv()) {
            std::fputs(Line->c_str(), stdout);
            std::fputc('\n', stdout);
            std::fflush(stdout);
        }
    });
}

/// 每 150ms 检查一次 stop 文件，出现即请求停止。
std::thread start_stop_file_watcher(const std::optional<fs::path> &StopFile, std::atomic<bool> &Finished) {
    return std::thread([StopFile, &Finished] {
        while (!Finished.load()) {
            std::error_code Code;
            if (StopFile && fs::is_regular_file(*StopFile, Code)) {
                GlobalStop->request();
                return;
            }
            if (GlobalStop->wait_for(std::chrono::milliseconds(150))) {
                return;
            }
        }
    });
}

/// --serve：只启动云服务，把连接参数写盘后一直运行到停止或房间失败。
void serve_room(const fs::path &Root, const Arguments &Args, std::shared_ptr<LogChannel> Logs) {
    require(Args.Scripts.has_value(), "--serve requires --scripts");
    require(Args.Archives.has_value(), "--serve requires --archives");
    std::error_code Code;
    fs::create_directories(*Args.Archives, Code);
    require(!Code, std::format("创建存档目录失败：{}", Code.message()));

    HostConfig Config;
    Config.Executable = Root / L"build" / L"Release" / L"script_host.exe";
    Config.Dll = Root / L"scriptrunner.dll";
    Config.Scripts = absolute_path(*Args.Scripts);
    Config.Archives = absolute_path(*Args.Archives);
    std::unique_ptr<Room> Active = Room::start(Config, Args.Direct ? std::nullopt : std::optional<std::size_t>(0), Logs);

    Json Profiles = Json::array();
    for (const Profile &Entry : Active->profiles()) {
        Profiles.push_back(Entry.to_json());
    }
    atomic_write(*Args.Serve, dump_pretty(Profiles));
    std::printf("ROOM_READY %s\n", Active->profiles()[0].Room.c_str());
    std::fflush(stdout);

    std::optional<std::string> Failure;
    std::uint64_t Seen = 0;
    while (!GlobalStop->is_requested()) {
        Active->wait_status_change(Seen, std::chrono::milliseconds(100));
        const RoomStatus Status = Active->status();
        if (Status.Failure) {
            Failure = Status.Failure;
            break;
        }
    }
    Active->close();
    if (Failure) {
        throw Error(*Failure);
    }
}

void apply_overrides(Config &Settings, const Arguments &Args) {
    const std::pair<fs::path *, const std::optional<fs::path> *> Paths[] = {
        {&Settings.Map, &Args.Map},
        {&Settings.LocalEntry, &Args.LocalEntry},
        {&Settings.CloudScripts, &Args.CloudScripts},
        {&Settings.Archives, &Args.Archives},
        {&Settings.Editor, &Args.Editor},
        {&Settings.SourceGame, &Args.SourceGame},
        {&Settings.Game, &Args.Game},
    };
    for (const auto &[Target, Override] : Paths) {
        if (*Override) {
            *Target = absolute_path(**Override);
        }
    }
    if (Args.StartupTimeout) {
        Settings.StartupTimeout = *Args.StartupTimeout;
    }
    if (Args.Seconds) {
        Settings.Seconds = *Args.Seconds;
    }
}

int run(const Arguments &Args) {
    const fs::path Root = find_root();
    auto Logs = std::make_shared<LogChannel>(2048);
    std::atomic<bool> Finished{false};
    std::thread Printer = start_printer(Logs);
    std::thread Watcher = start_stop_file_watcher(Args.StopFile, Finished);

    std::optional<std::string> Failure;
    try {
        if (Args.Serve) {
            serve_room(Root, Args, Logs);
        } else {
            const fs::path Path = Args.ConfigPath ? *Args.ConfigPath : default_config(Root);
            Config Settings = Config::load(Path);
            apply_overrides(Settings, Args);
            if (Args.Check) {
                check_setup(Root, Settings);
                Logs->try_send("配置检查通过；启动时还会检查端口和存档锁。");
            } else {
                run_session(Root, Settings, GlobalStop, Logs);
            }
        }
    } catch (const std::exception &Reason) {
        Failure = Reason.what();
    }
    Logs->close();
    Printer.join();
    Finished.store(true);
    GlobalStop->request();
    Watcher.join();
    if (Failure) {
        std::fprintf(stderr, "Error: %s\n", Failure->c_str());
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(on_console_event, TRUE);
    try {
        const Arguments Args = parse_arguments(command_line_tokens());
        if (Args.ShowHelp) {
            print_help();
            return 0;
        }
        if (Args.ShowVersion) {
            std::printf("ScriptRoomCLI %s\n", Version);
            return 0;
        }
        return run(Args);
    } catch (const std::exception &Failure) {
        std::fprintf(stderr, "Error: %s\n", Failure.what());
        return 2;
    }
}
