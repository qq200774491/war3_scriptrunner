#include "Launch.h"

#include "ClientArchives.h"
#include "Crypto.h"
#include "Error.h"
#include "FileSystem.h"
#include "GameMonitor.h"
#include "GameProcess.h"
#include "Ini.h"
#include "Json.h"
#include "ChildProcess.h"
#include "Socket.h"
#include "Text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <format>
#include <fstream>
#include <thread>

namespace scriptroom {

namespace {

using Clock = std::chrono::steady_clock;
using Millis = std::chrono::milliseconds;
using Seconds = std::chrono::seconds;

constexpr const char *GameHash = "e04d1716603c075eb0c8e1e21cf1093a664adc5249efab396bfa08d7b09d0c3a";
constexpr std::array<const char *, 11> GameFiles = {"War3.exe",     "Game.dll",       "Storm.dll",
                                                    "Mss32.dll",    "ijl15.dll",      "msvcr120.dll",
                                                    "msvcp120.dll", "war3.mpq",       "War3x.mpq",
                                                    "War3xLocal.mpq", "War3Patch.mpq"};
constexpr std::array<const char *, 4> RequiredPlugins = {"yd_mutil_warcraft3.dll", "yd_lua_engine.dll", "kkapi.dll",
                                                 "kkapi_local_plugin.dll"};
constexpr std::uint16_t LanPort = 6112;

fs::path from_utf8(const char *Text) {
    return path_from_utf8(Text);
}

std::string shown(const fs::path &Path) {
    return path_to_utf8(Path);
}

bool same_file_content(const fs::path &Source, const fs::path &Destination) {
    std::error_code Code;
    return fs::exists(Destination, Code) && fs::file_size(Source, Code) == fs::file_size(Destination, Code) &&
           file_digest(Source) == file_digest(Destination);
}

/// 仅当内容不同才复制，避免无谓覆盖运行中的文件。
void copy_if_changed(const fs::path &Source, const fs::path &Destination) {
    if (same_file_content(Source, Destination)) {
        return;
    }
    std::error_code Code;
    fs::create_directories(Destination.parent_path(), Code);
    require(!Code, std::format("创建目录失败：{}", Code.message()));
    fs::copy_file(Source, Destination, fs::copy_options::overwrite_existing, Code);
    require(!Code, std::format("复制 {} 到 {}: {}", shown(Source), shown(Destination), Code.message()));
}

/// 读取环境变量（UTF-8）；不存在返回空串。
std::string environment_value(const wchar_t *Name) {
    const DWORD Size = GetEnvironmentVariableW(Name, nullptr, 0);
    if (Size == 0) {
        return {};
    }
    std::wstring Value(Size, L'\0');
    const DWORD Length = GetEnvironmentVariableW(Name, Value.data(), Size);
    Value.resize(Length);
    return utf8_lossy(to_utf8(Value));
}

Ini parse_cfg(const fs::path &Path) {
    return Ini::parse_text(read_utf8_file(Path));
}

fs::path native_dir(const fs::path &Root) {
    return Root / L"build" / L"Release";
}

fs::path game_work_dir(const fs::path &Root) {
    return Root / L"build" / L"game";
}

/// 读取文件，缺失时给出带路径的中文提示。
void require_file(const fs::path &Path) {
    std::error_code Code;
    require(fs::is_regular_file(Path, Code), std::format("缺少文件：{}", shown(Path)));
}

/// 准备共用魔兽目录、客户端存档、地图、插件与启动配置；返回两位玩家的 INI 与编辑器暂存目录。
struct Prepared {
    std::array<fs::path, 2> ClientArchives;
    fs::path StagedEditor;
};

Prepared prepare(const fs::path &Root, const Config &Settings) {
    check_setup(Root, Settings);
    require(Socket::port_is_free(LanPort), "6112 端口被占用，请先退出上一轮魔兽");

    std::error_code Code;
    for (const fs::path &Executable : {Settings.Game / L"war3.exe", Settings.Archives / L"clients" / L"player-0" / L"war3.exe",
                                       Settings.Archives / L"clients" / L"player-1" / L"war3.exe"}) {
        if (fs::exists(Executable, Code)) {
            probe_writable(Executable);
        }
    }
    for (const char *Name : GameFiles) {
        copy_if_changed(Settings.SourceGame / from_utf8(Name), Settings.Game / from_utf8(Name));
    }

    bool HasPrevious = false;
    if (fs::exists(Settings.Archives, Code)) {
        for (const auto &Entry : fs::directory_iterator(Settings.Archives, Code)) {
            if (Entry.path().extension() == L".archive") {
                HasPrevious = true;
                break;
            }
        }
    }
    const fs::path Legacy = Settings.Game / L"dz_w3_plugin.ini";
    Prepared Result;
    Result.ClientArchives = prepare_client_archives(Settings.Archives / L"clients",
                                                    HasPrevious ? std::optional<fs::path>(Legacy) : std::nullopt);
    copy_if_changed(Settings.Map, Settings.Game / L"Maps" / L"Test" / L"Project.w3x");

    const fs::path Folder = game_work_dir(Root);
    Result.StagedEditor = Folder / L"editor";
    const fs::path Plugins = Result.StagedEditor / L"plugin" / L"warcraft3";
    for (const auto &Entry : fs::directory_iterator(Settings.Editor / L"plugin" / L"warcraft3")) {
        if (Entry.is_regular_file() && equals_ignore_case(path_to_utf8(Entry.path().extension()), ".dll")) {
            copy_if_changed(Entry.path(), Plugins / Entry.path().filename());
        }
    }
    copy_if_changed(native_dir(Root) / L"zz_shared_quicktest.dll", Plugins / L"zz_shared_quicktest.dll");

    Ini Config = parse_cfg(Settings.Editor / L"plugin" / L"warcraft3" / L"config.cfg");
    Ini::Fields &Enable = Config.section_mut("Enable");
    for (auto Entry = Enable.begin(); Entry != Enable.end();) {
        Entry = equals_ignore_case(Entry->first, "zz_shared_quicktest.dll") ? Enable.erase(Entry) : std::next(Entry);
    }
    Enable["zz_shared_quicktest.dll"] = "1";
    atomic_write(Plugins / L"config.cfg", Config.text());

    copy_if_changed(Settings.Editor / L"bin" / L"EverConfig.cfg", Result.StagedEditor / L"bin" / L"EverConfig.cfg");
    copy_if_changed(Settings.Editor / L"plugin" / L"warcraft3" / L"yd_loader.dll", Folder / L"yd_loader.dll");
    return Result;
}

/// 生成客户端入口脚本：先安装 legacy_mls，再接原入口，写进共用魔兽目录。
void install_entry(const fs::path &Root, const Config &Settings, const Room &Active) {
    const fs::path Folder = game_work_dir(Root);
    std::string Players;
    for (const Profile &Entry : Active.profiles()) {
        if (!Players.empty()) {
            Players += ',';
        }
        Players += std::format("[{}]={{port={},token={}}}", Entry.Player, Entry.Port, lua_literal(Entry.Token));
    }
    const fs::path Source = Settings.LocalEntry.parent_path();
    require(!Source.empty(), "无客户端脚本目录");
    std::string SearchPath;
    for (const fs::path &Pattern : {Root / L"map" / L"?.lua", Source / L"?.lua", Source / L"?" / L"init.lua"}) {
        std::string Text = shown(Pattern);
        std::replace(Text.begin(), Text.end(), '\\', '/');
        SearchPath += (SearchPath.empty() ? "" : ";") + Text;
    }
    const std::string Original = read_utf8_file(Settings.LocalEntry);
    const std::string Entry = std::format(
        "package.path = {} .. ';' .. package.path\n"
        "require('legacy_mls').install({{room={},relay=0,sync_chunk=350,sync_immediate=true,logs={},dll={},players={{{}}}}})\n"
        "{}",
        lua_literal(SearchPath), lua_literal(Active.profiles()[0].Room), lua_literal(shown(Folder)),
        lua_literal(shown(native_dir(Root) / L"shared_room_native.dll")), Players, Original);
    require(Settings.LocalEntry.has_filename(), "无入口文件名");
    atomic_write(Settings.Game / Settings.LocalEntry.filename(), Entry);
    for (int Player = 0; Player < 2; ++Player) {
        atomic_write(Folder / std::format(L"mls-player-{}.log", Player), "");
    }
}

/// 启动一个魔兽客户端，由 lua_probe 助手持有其创建句柄。
GameMonitor launch_client(const fs::path &Root, const Config &Settings, const fs::path &Staged, const fs::path &Ini,
                          const std::string &RoomId, std::size_t Player) {
    const fs::path Folder = game_work_dir(Root);
    const fs::path Log = Folder / std::format(L"quicktest-{}.log", Player);
    atomic_write(Log, "");

    std::vector<std::string> Args = {shown(Settings.Game / L"war3.exe"), "-window", "-opengl", "-kkwe", shown(Staged)};
    if (Player == 0) {
        Args.insert(Args.end(), {"-host", "2", "Maps\\Test\\Project.w3x"});
    } else {
        Args.push_back("-join");
    }
    Args.insert(Args.end(), {"--qt-room", RoomId, "--qt-player", std::format("SRPlayer{}", Player), "--qt-log",
                             shown(Log), "--qt-ini", shown(Ini)});
    std::string Command;
    for (const std::string &Argument : Args) {
        Command += (Command.empty() ? "" : " ") + windows_argument(Argument);
    }

    const std::array<std::pair<const char *, std::string>, 4> Fields = {{
        {"editor", shown(Settings.Editor)},
        {"game", shown(Settings.Game)},
        {"files", shown(Folder)},
        {"command", Command},
    }};
    std::string Table = "return {";
    for (std::size_t Index = 0; Index < Fields.size(); ++Index) {
        Table += (Index == 0 ? "" : ",") + std::format("{}={}", Fields[Index].first, lua_literal(Fields[Index].second));
    }
    Table += "}\n";
    atomic_write(Folder / L"launch_config.lua", Table);
    atomic_write(Folder / std::format(L"launch-{}.json", Player),
                 dump_pretty(Json{{"command", Command}, {"game", shown(Settings.Game)}, {"editor", shown(Staged)}}));

    ChildSpec Helper;
    Helper.Executable = native_dir(Root) / L"lua_probe.exe";
    Helper.Arguments = {shown(Settings.Editor / L"bin" / L"luacore.dll"), shown(Root / L"tools" / L"launch_game.lua")};
    Helper.WorkingDirectory = Root;
    std::string Path = shown(Settings.Editor / L"bin");
    const std::string Inherited = environment_value(L"PATH");
    if (!Inherited.empty()) {
        Path += ";" + Inherited;
    }
    Helper.Environment = {{"PATH", Path}, {"SR_MONITOR_GAME", "1"}};
    return GameMonitor::start(Helper, Folder / std::format(L"launch-{}.stderr.log", Player));
}

/// 命名事件：房主与插件之间的就绪、已开局、失败信号。
bool quicktest_signaled(const std::string &RoomId, const char *Suffix) {
    return event_signaled(std::format("Local\\SharedScriptQuicktest-{}-{}", RoomId, Suffix));
}

void ensure_game_running(GameMonitor &Client, std::size_t Player) {
    if (const std::optional<std::uint32_t> Code = Client.exit_code()) {
        throw Error(std::format(
            "{}（玩家 {}，PID {}）在双人开局前已退出，退出码 0x{:08X}；请检查地图加载及 quicktest-{}.log，云端读档尚未开始",
            Player == 0 ? "房主" : "加入端", Player, Client.pid(), *Code, Player));
    }
}

/// 等待房主进入可加入状态；返回 false 表示收到停止请求。
bool wait_host(const std::string &RoomId, double Seconds, GameMonitor &Host, const StopSignal &Stop) {
    const Clock::time_point Start = Clock::now();
    while (true) {
        if (Stop.is_requested()) {
            return false;
        }
        ensure_game_running(Host, 0);
        require(!quicktest_signaled(RoomId, "failed"), "房主报告启动失败，请检查 quicktest-0.log");
        if (quicktest_signaled(RoomId, "ready")) {
            return true;
        }
        require(std::chrono::duration<double>(Clock::now() - Start).count() < Seconds, "房主启动超时");
        Stop.wait_for(Millis(50));
    }
}

/// 一轮运行的日志：同时写入文件和界面队列，由独立线程串行落盘。
class RunLog {
public:
    RunLog(const fs::path &Folder, std::shared_ptr<LogChannel> Output, std::shared_ptr<RunStats> Stats)
        : Queue(std::make_shared<LogChannel>(2048)), Output(std::move(Output)), Stats(std::move(Stats)) {
        const std::int64_t Timestamp =
            std::chrono::duration_cast<Seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        FilePath = Folder / std::format(L"cloud-cpp-{}.log", Timestamp);
        File.open(FilePath, std::ios::binary | std::ios::trunc);
        require(File.is_open(), std::format("无法创建日志：{}", shown(FilePath)));
        Writer = std::thread([this] {
            while (std::optional<std::string> Line = Queue->recv()) {
                File << *Line << '\n';
                File.flush();
                if (!File) {
                    Failed = true;
                }
                this->Stats->record_log_lines(1 + static_cast<std::uint64_t>(std::count(Line->begin(), Line->end(), '\n')));
                std::string Copy = *Line;
                this->Output->try_send(std::move(Copy));
            }
        });
    }

    ~RunLog() { finish_quietly(); }
    RunLog(const RunLog &) = delete;
    RunLog &operator=(const RunLog &) = delete;

    const std::shared_ptr<LogChannel> &sender() const { return Queue; }
    const fs::path &path() const { return FilePath; }

    void log(std::string Line) { Queue->send(std::move(Line)); }

    /// 关闭队列并等待落盘完成；写入失败时抛出 Error。
    void finish() {
        finish_quietly();
        require(!Failed, "写入运行日志失败");
    }

private:
    void finish_quietly() {
        Queue->close();
        if (Writer.joinable()) {
            Writer.join();
        }
    }

    std::shared_ptr<LogChannel> Queue;
    std::shared_ptr<LogChannel> Output;
    std::shared_ptr<RunStats> Stats;
    fs::path FilePath;
    std::ofstream File;
    std::thread Writer;
    bool Failed = false;
};

/// 把游戏日志文件的新增内容转给界面。
void forward_game_logs(const fs::path &Folder, std::array<std::string, 2> &Previous, RunLog &Log) {
    for (std::size_t Player = 0; Player < Previous.size(); ++Player) {
        const std::string Text = utf8_lossy(read_file(Folder / std::format(L"quicktest-{}.log", Player)));
        if (Text == Previous[Player]) {
            continue;
        }
        const bool Extends = Text.starts_with(Previous[Player]);
        std::string Fresh = Extends ? Text.substr(Previous[Player].size()) : Text;
        while (!Fresh.empty() && std::isspace(static_cast<unsigned char>(Fresh.back()))) {
            Fresh.pop_back();
        }
        Log.log(std::move(Fresh));
        Previous[Player] = Text;
    }
}

/// 这一轮客户端监测与启动阶段由同一所有者维护；服务停止后游戏继续运行。
class GameSession {
public:
    GameSession(const fs::path &Root, const Config &Settings, const Room &Active)
        : Root(Root), Settings(Settings), Active(Active) {}

    LaunchPhase phase() const { return Phase; }

    void start(const Prepared &Setup, const StopSignal &Stop, RunLog &Log) {
        install_entry(Root, Settings, Active);
        const std::string &RoomId = Active.profiles()[0].Room;
        for (std::size_t Player = 0; Player < Setup.ClientArchives.size(); ++Player) {
            if (Stop.is_requested()) {
                return;
            }
            if (Player == 1 && !wait_host(RoomId, Settings.StartupTimeout, Clients[0], Stop)) {
                return;
            }
            if (Stop.is_requested()) {
                return;
            }
            Clients.push_back(launch_client(Root, Settings, Setup.StagedEditor, Setup.ClientArchives[Player], RoomId, Player));
            Log.log(std::format("[启动] 玩家 {} PID {}；INI {}", Player, Clients.back().pid(),
                                shown(Setup.ClientArchives[Player])));
        }
    }

    void monitor(const StopSignal &Stop, RunLog &Log) {
        const fs::path Folder = game_work_dir(Root);
        const std::string &RoomId = Active.profiles()[0].Room;
        const Clock::time_point Start = Clock::now();
        std::array<std::string, 2> Previous;
        Clock::time_point Report = Clock::now();
        while (!Stop.is_requested()) {
            if (Stop.wait_for(Seconds(1))) {
                break;
            }
            if (std::chrono::duration_cast<Seconds>(Clock::now() - Start).count() >=
                static_cast<std::int64_t>(Settings.Seconds)) {
                break;
            }
            const RoomStatus Status = Active.status();
            if (Status.Failure) {
                throw Error(*Status.Failure);
            }
            require(!quicktest_signaled(RoomId, "failed"), "房主报告启动失败，请检查 quicktest-0.log");
            observe_phase(Phase, quicktest_signaled(RoomId, "started"));
            if (Phase == LaunchPhase::WaitingForLan) {
                for (std::size_t Player = 0; Player < Clients.size(); ++Player) {
                    ensure_game_running(Clients[Player], Player);
                }
                require(std::chrono::duration<double>(Clock::now() - Start).count() < Settings.StartupTimeout,
                        "双人开局超时，请检查 quicktest 日志");
            }
            forward_game_logs(Folder, Previous, Log);
            if (Clock::now() - Report >= Seconds(10)) {
                Log.log(std::format("[运行] 在线 {}/2；回包 {}；云端 {}", Status.Connected, Status.Delivery,
                                    Status.Active ? "运行中" : "等待连接"));
                Report = Clock::now();
            }
        }
    }

    /// 放弃对客户端助手的所有权并返回游戏 PID。
    std::vector<std::uint32_t> finish() {
        std::vector<std::uint32_t> Pids;
        for (GameMonitor &Client : Clients) {
            Pids.push_back(Client.pid());
            Client.detach();
        }
        return Pids;
    }

private:
    const fs::path &Root;
    const Config &Settings;
    const Room &Active;
    std::vector<GameMonitor> Clients;
    LaunchPhase Phase = LaunchPhase::WaitingForLan;
};

void save_report(const fs::path &Folder, const std::string &RoomId, const std::vector<std::uint32_t> &Pids,
                 LaunchPhase Phase, const RoomStatus &Status, const std::optional<std::string> &Error,
                 const Prepared &Setup, const Config &Settings, const fs::path &ConsoleLog) {
    Json Body = {
        {"runtime", "cpp"},
        {"room", RoomId},
        {"game_pids", Pids},
        {"phase", phase_name(Phase)},
        {"cloud_callbacks", Status.Delivery},
        {"cloud_failure", Status.Failure ? Json(*Status.Failure) : Json(nullptr)},
        {"error", Error ? Json(*Error) : Json(nullptr)},
        {"client_archives", {shown(Setup.ClientArchives[0]), shown(Setup.ClientArchives[1])}},
        {"game", shown(Settings.Game)},
        {"console_log", shown(ConsoleLog)},
    };
    atomic_write(Folder / L"project-result-cpp.json", dump_pretty(Body));
}

} // namespace

void observe_phase(LaunchPhase &Phase, bool Started) {
    if (Started) {
        Phase = LaunchPhase::LanStarted;
    }
}

const char *phase_name(LaunchPhase Phase) {
    return Phase == LaunchPhase::LanStarted ? "lan_started" : "waiting_for_lan";
}

std::string lua_literal(std::string_view Text) {
    const bool Ascii = std::all_of(Text.begin(), Text.end(), [](char Unit) { return static_cast<unsigned char>(Unit) < 0x80; });
    std::string Result = "\"";
    for (const char Unit : Text) {
        const unsigned char Byte = static_cast<unsigned char>(Unit);
        if (!Ascii) {
            Result += std::format("\\{:03}", Byte);
        } else if (Unit == '"') {
            Result += "\\\"";
        } else if (Unit == '\\') {
            Result += "\\\\";
        } else if (Byte < 32 || Byte == 127) {
            Result += std::format("\\{:03}", Byte);
        } else {
            Result.push_back(Unit);
        }
    }
    Result.push_back('"');
    return Result;
}

void check_setup(const fs::path &Root, const Config &Settings) {
    Settings.validate_numbers();
    for (const fs::path &Path : {Settings.Map, Settings.LocalEntry, Settings.CloudScripts / L"main.lua",
                                 Settings.Editor / L"bin" / L"luacore.dll", Root / L"scriptrunner.dll",
                                 Root / L"map" / L"legacy_mls.lua", Root / L"tools" / L"launch_game.lua",
                                 Root / L"tools" / L"game_control.lua"}) {
        require_file(Path);
    }
    std::error_code Code;
    for (const char *Name : {"script_host.exe", "lua_probe.exe", "shared_room_native.dll", "zz_shared_quicktest.dll"}) {
        require(fs::is_regular_file(native_dir(Root) / from_utf8(Name), Code), std::format("缺少原生组件：{}", Name));
    }
    for (const char *Name : GameFiles) {
        require(fs::is_regular_file(Settings.SourceGame / from_utf8(Name), Code), std::format("缺少魔兽文件：{}", Name));
    }
    const fs::path PluginFolder = Settings.Editor / L"plugin" / L"warcraft3";
    const Ini Enabled = parse_cfg(PluginFolder / L"config.cfg");
    for (const char *Name : RequiredPlugins) {
        require(fs::is_regular_file(PluginFolder / from_utf8(Name), Code) && Enabled.get("Enable", Name) == "1",
                std::format("编辑器插件缺失或未启用：{}", Name));
    }
    const Sha256Digest Digest = file_digest(Settings.SourceGame / L"Game.dll");
    require(hex_encode(Digest) == GameHash, "Game.dll 不是已验证的 1.27 版本，停止启动");

    const fs::path Source = fs::canonical(Settings.SourceGame, Code);
    require(!Code, std::format("无法解析原魔兽目录：{}", Code.message()));
    const fs::path Game = fs::exists(Settings.Game, Code) ? fs::canonical(Settings.Game, Code) : absolute_path(Settings.Game);
    require(comparable_path(Source) != comparable_path(Game), "测试目录不能覆盖原魔兽目录");
}

void run_session(const fs::path &Root, const Config &Settings, StopHandle Stop, std::shared_ptr<LogChannel> Output,
                 std::shared_ptr<RunStats> Stats) {
    const fs::path Folder = game_work_dir(Root);
    std::error_code Code;
    fs::create_directories(Folder, Code);
    require(!Code, std::format("创建目录失败：{}", Code.message()));
    const FileLock Exclusive = launch_lock(Folder / L".project-launch.lock");

    const Prepared Setup = prepare(Root, Settings);
    if (Stop->is_requested()) {
        return;
    }
    const std::shared_ptr<RunStats> Recorder = stats_or_private(std::move(Stats));
    RunLog Log(Folder, std::move(Output), Recorder);

    HostConfig Host;
    Host.Executable = native_dir(Root) / L"script_host.exe";
    Host.Dll = Root / L"scriptrunner.dll";
    Host.Scripts = Settings.CloudScripts;
    Host.Archives = Settings.Archives;
    std::unique_ptr<Room> Active;
    Recorder->begin(2);
    try {
        Active = Room::start(Host, 0, Log.sender(), Recorder);
    } catch (...) {
        Recorder->end();
        Log.finish();
        throw;
    }
    const std::string RoomId = Active->profiles()[0].Room;
    Log.log(std::format("[房间] {}\n[云脚本] {}\n[存档] {}\n[共用魔兽] {}\n[控制台] {}", RoomId, shown(Settings.CloudScripts),
                        shown(Settings.Archives), shown(Settings.Game), shown(Log.path())));

    GameSession Session(Root, Settings, *Active);
    std::optional<std::string> Failure;
    try {
        Session.start(Setup, *Stop, Log);
        Session.monitor(*Stop, Log);
    } catch (const std::exception &Reason) {
        Failure = Reason.what();
    }
    const LaunchPhase Phase = Session.phase();
    const std::vector<std::uint32_t> Pids = Session.finish();
    const RoomStatus Status = Active->status();
    Active->close();
    Recorder->end();

    std::optional<std::string> ReportFailure;
    try {
        save_report(Folder, RoomId, Pids, Phase, Status, Failure, Setup, Settings, Log.path());
    } catch (const std::exception &Reason) {
        ReportFailure = Reason.what();
    }
    Log.log("[停止] " + (Failure ? *Failure : std::string("云服务已停止")));
    Log.finish();
    if (ReportFailure) {
        throw Error(*ReportFailure);
    }
    if (Failure) {
        throw Error(*Failure);
    }
}

} // namespace scriptroom
