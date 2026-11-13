// 直接加载原版 DLL，验证 C++ 服务与已有 TCP / 宿主协议的真实边界。
// 需要 build/Release/script_host.exe（由上级 CMake 以 Win32 构建）。

#include "Config.h"
#include "Error.h"
#include "FileSystem.h"
#include "GlobalArchive.h"
#include "Host.h"
#include "Room.h"
#include "RunStats.h"
#include "Socket.h"
#include "TestHarness.h"
#include "Text.h"

#include <deque>
#include <format>
#include <thread>

namespace {

using namespace scriptroom;
using namespace testing;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

constexpr auto Patience = 25s;

fs::path root() {
    return project_root();
}

HostConfig host_config(const fs::path &Scripts, const fs::path &Archives) {
    HostConfig Config;
    Config.Executable = root() / L"build" / L"Release" / L"script_host.exe";
    Config.Dll = root() / L"scriptrunner.dll";
    Config.Scripts = Scripts;
    Config.Archives = Archives;
    if (!fs::exists(Config.Executable) || !fs::exists(Config.Dll)) {
        throw Skipped("缺少 script_host.exe 或 scriptrunner.dll");
    }
    return Config;
}

/// 房间客户端：换行分隔的 JSON，按类型挑选消息，其余缓存。
class Client {
public:
    explicit Client(const Profile &Identity) : Connection(Socket::connect_loopback(Identity.Port)) {
        send(Identity.to_json());
        expect("joined");
    }

    explicit Client(Socket Raw) : Connection(std::move(Raw)) {}

    void send(const Json &Body) {
        const std::string Line = dump_compact(Body) + '\n';
        Connection.send_all(Line.data(), Line.size());
    }

    Json expect(std::string_view Kind) {
        for (auto Item = Inbox.begin(); Item != Inbox.end(); ++Item) {
            if (Item->value("kind", "") == Kind) {
                Json Found = std::move(*Item);
                Inbox.erase(Item);
                return Found;
            }
        }
        const auto Deadline = std::chrono::steady_clock::now() + Patience;
        while (true) {
            Json Body = read_line(Deadline);
            if (Body.value("kind", "") == Kind) {
                return Body;
            }
            check(Body.value("kind", "") != "room_failed", "意外的房间故障：" + dump_compact(Body));
            Inbox.push_back(std::move(Body));
        }
    }

    /// 发送请求并取回脚本回包与 ACK。
    std::pair<Json, Json> exchange(const std::string &Id, const std::string &Event, const std::string &Payload) {
        send(Json{{"id", Id}, {"event", Event}, {"payload", Payload}});
        Json Reply = expect("event");
        Json Ack = expect("ack");
        return {std::move(Reply), std::move(Ack)};
    }

private:
    Json read_line(std::chrono::steady_clock::time_point Deadline) {
        static const std::atomic<bool> Never{false};
        while (true) {
            if (const std::size_t Newline = Pending.find('\n'); Newline != std::string::npos) {
                const Json Body = parse_json(Pending.substr(0, Newline));
                Pending.erase(0, Newline + 1);
                return Body;
            }
            char Buffer[8192];
            std::size_t Got = 0;
            const auto Left = std::chrono::duration_cast<std::chrono::milliseconds>(Deadline - std::chrono::steady_clock::now());
            check(Left.count() > 0, "等待房间消息超时");
            const IoStatus Status = Connection.recv_some(Buffer, sizeof(Buffer), Got, Left, Never);
            check(Status != IoStatus::Eof, "连接在等待消息时被关闭");
            check(Status == IoStatus::Ok, "等待房间消息超时");
            Pending.append(Buffer, Got);
        }
    }

    Socket Connection;
    std::string Pending;
    std::deque<Json> Inbox;
};

struct TestRoom {
    TempDirectory State;
    std::shared_ptr<LogChannel> Logs = std::make_shared<LogChannel>(2048);
    std::shared_ptr<RunStats> Stats = std::make_shared<RunStats>();
    std::unique_ptr<Room> Active;
    std::unique_ptr<Client> Zero;
    std::unique_ptr<Client> One;
    std::atomic<bool> Draining{true};
    std::thread Drain;

    explicit TestRoom(std::optional<std::size_t> Relay) {
        Drain = std::thread([this] {
            while (Logs->recv()) {
            }
        });
        Stats->begin(2);
        Active = Room::start(host_config(root() / L"tests" / L"scripts" / L"counter", State.path()), Relay, Logs, Stats);
        Zero = std::make_unique<Client>(Active->profiles()[0]);
        One = std::make_unique<Client>(Active->profiles()[1]);
        Zero->expect("ready");
        One->expect("ready");
    }

    ~TestRoom() {
        Zero.reset();
        One.reset();
        Active.reset();
        Logs->close();
        Drain.join();
    }
};

/// 任意人数的房间：所有玩家连上后等 ready。
struct CrowdRoom {
    TempDirectory State;
    std::shared_ptr<LogChannel> Logs = std::make_shared<LogChannel>(2048);
    std::unique_ptr<Room> Active;
    std::vector<std::unique_ptr<Client>> Players;
    std::thread Drain;

    explicit CrowdRoom(std::size_t Count) {
        Drain = std::thread([this] {
            while (Logs->recv()) {
            }
        });
        HostConfig Config = host_config(root() / L"tests" / L"scripts" / L"counter", State.path());
        Config.Players = Count;
        Active = Room::start(Config, std::nullopt, Logs);
        check_equal(Active->profiles().size(), Count);
        for (const Profile &Identity : Active->profiles()) {
            Players.push_back(std::make_unique<Client>(Identity));
        }
    }

    ~CrowdRoom() {
        Players.clear();
        Active.reset();
        Logs->close();
        Drain.join();
    }
};

const Registrar SinglePlayerRoom("room.single_player_starts_without_teammates", [] {
    CrowdRoom Room(1);
    const Json Ready = Room.Players[0]->expect("ready");
    check_equal(Ready["players"], Json::array({0}));
    check_equal(Room.Players[0]->exchange("solo", "increment", "4").first["payload"], "4");
    check_equal(Room.Active->status().Connected, std::size_t(1));
});

const Registrar ThreePlayerRoom("room.three_players_wait_for_everyone", [] {
    CrowdRoom Room(3);
    for (const auto &Player : Room.Players) {
        check_equal(Player->expect("ready")["players"], Json::array({0, 1, 2}));
    }
    check_equal(Room.Players[2]->exchange("a", "increment", "5").first["payload"], "5");
    check_equal(Room.Players[1]->exchange("b", "increment", "2").first["payload"], "7");
    Room.Players[2]->send(Json{{"id", "all"}, {"event", "broadcast"}, {"payload", "hi"}});
    for (const auto &Player : Room.Players) {
        check_equal(Player->expect("event")["payload"], "hi");
    }
    Room.Players[2]->expect("ack");

    // 只认这一轮的玩家号：多出来的身份进不来。
    Json Forged = Room.Active->profiles()[0].to_json();
    Forged["player"] = 3;
    Client Stranger(Socket::connect_loopback(Room.Active->profiles()[0].Port));
    Stranger.send(Forged);
    check(Stranger.expect("error")["reason"].get<std::string>().find("identity") != std::string::npos);
});

const Registrar RoomWaitsForLastPlayer("room.host_waits_until_all_players_join", [] {
    TempDirectory State;
    auto Logs = std::make_shared<LogChannel>(2048);
    std::thread Drain([&] {
        while (Logs->recv()) {
        }
    });
    {
        HostConfig Config = host_config(root() / L"tests" / L"scripts" / L"counter", State.path());
        Config.Players = 3;
        const std::unique_ptr<Room> Active = Room::start(Config, std::nullopt, Logs);
        Client Zero(Active->profiles()[0]);
        Client One(Active->profiles()[1]);
        std::this_thread::sleep_for(300ms);
        check(!Active->status().Active, "还差一名玩家时不该启动宿主");
        check_equal(Active->status().Connected, std::size_t(2));
        check_throws([&] {
            HostConfig Wrong = Config;
            Wrong.Players = 13;
            Room::start(Wrong, std::nullopt, Logs);
        }, "player count");
    }
    Logs->close();
    Drain.join();
});

const Registrar RealDllSharedState("room.real_dll_shared_state_dedup_and_identity", [] {
    TestRoom Room(std::nullopt);
    auto [Reply, Ack] = Room.Zero->exchange("same", "increment", "7");
    check_equal(Reply["payload"], "7");
    check_equal(Reply["player"], 0);
    check_equal(Room.One->exchange("same", "increment", "5").first["payload"], "12");

    Room.Zero->send(Json{{"id", "same"}, {"event", "increment"}, {"payload", "7"}});
    const Json Repeat = Room.Zero->expect("ack");
    check_equal(Repeat["duplicate"], true);
    check_equal(Repeat["sequence"], Ack["sequence"]);

    Room.Zero->send(Json{{"id", "same"}, {"event", "increment"}, {"payload", "100"}});
    check(Room.Zero->expect("error")["reason"].get<std::string>().find("different content") != std::string::npos);

    Room.Zero->send(Json{{"id", "spoof"}, {"event", "increment"}, {"payload", "100"}, {"player", 1}});
    Room.Zero->expect("error");
    check_equal(Room.One->exchange("query", "query", "").first["payload"], "12");
});

const Registrar RoomRecordsStats("room.records_request_stats_and_events", [] {
    TestRoom Room(std::nullopt);
    Room.Zero->exchange("a", "increment", "1");
    Room.Zero->exchange("b", "increment", "1");
    Room.One->exchange("c", "increment", "1");
    Room.Zero->send(Json{{"id", "a"}, {"event", "increment"}, {"payload", "changed"}});
    Room.Zero->expect("error");

    const RunStatsSnapshot Now = Room.Stats->snapshot();
    check_equal(Now.Players[0].Latency.Count, std::uint64_t(3));
    check_equal(Now.Players[0].Failures, std::uint64_t(1));
    check_equal(Now.Players[1].Latency.Count, std::uint64_t(1));
    check_equal(Now.Players[1].Failures, std::uint64_t(0));
    check(Now.Players[0].Latency.MaxMs > 0.0, "请求耗时应大于零");
    check_equal(Now.RoomEvents, std::uint64_t(3));
    check(Now.PeakBacklog >= 1, "请求经过宿主队列，峰值至少为 1");
    check_equal(Now.Backlog, std::size_t(0));
});

const Registrar ConcurrentRequests("room.concurrent_requests_timer_broadcast_and_disconnect", [] {
    TestRoom Room(std::nullopt);
    for (int Index = 0; Index < 20; ++Index) {
        const std::string Id = std::to_string(Index);
        std::thread Other([&] { Room.One->exchange(Id, "increment", "1"); });
        Room.Zero->exchange(Id, "increment", "1");
        Other.join();
    }
    check_equal(Room.One->exchange("query", "query", "").first["payload"], "40");
    check_equal(Room.Zero->exchange("later", "later", "deferred").first["payload"], "deferred");

    Room.One->send(Json{{"id", "broadcast"}, {"event", "broadcast"}, {"payload", "both"}});
    check_equal(Room.Zero->expect("event")["payload"], "both");
    check_equal(Room.One->expect("event")["payload"], "both");
    Room.One->expect("ack");

    Room.Zero.reset();
    check(Room.One->expect("room_failed")["reason"].get<std::string>().find("disconnected") != std::string::npos);
});

const Registrar RelayAndLuaFailure("room.relay_and_lua_failure", [] {
    TestRoom Room(0);
    Room.One->send(Json{{"id", "relay"}, {"event", "increment"}, {"payload", "3"}});
    const Json Event = Room.Zero->expect("event");
    check_equal(Event["player"], 1);
    check_equal(Event["payload"], "3");
    check_equal(Event["delivery"], 1);
    Room.One->expect("ack");

    Room.Zero->send(Json{{"id", "failure"}, {"event", "fail"}, {"payload", ""}});
    check(Room.One->expect("room_failed")["reason"].get<std::string>().find("Lua callback") != std::string::npos);
});

const Registrar BadCredentialsKeepRoom("room.invalid_credentials_do_not_break_existing_room", [] {
    TestRoom Room(std::nullopt);
    Client Intruder(Socket::connect_loopback(Room.Active->profiles()[0].Port));
    Json Forged = Room.Active->profiles()[0].to_json();
    Forged["token"] = "invalid";
    Intruder.send(Forged);
    Intruder.expect("error");
    check_equal(Room.Zero->exchange("query", "query", "").first["payload"], "0");
});

const Registrar OversizedLineRejected("room.oversized_unauthenticated_line_is_dropped", [] {
    TestRoom Room(std::nullopt);
    Socket Raw = Socket::connect_loopback(Room.Active->profiles()[0].Port);
    const std::string Junk(300 * 1024, 'x');
    try {
        Raw.send_all(Junk.data(), Junk.size());
    } catch (const Error &) {
        // 服务端可能在发送完成前就关闭连接，属于预期。
    }
    check_equal(Room.Zero->exchange("query", "query", "").first["payload"], "0");
});

/// 等待宿主事件满足条件；遇到 HostFailed 立即失败。
template <typename Predicate> void wait_for_event(Channel<HostEvent> &Events, Predicate Match) {
    const auto Deadline = std::chrono::steady_clock::now() + Patience;
    while (std::chrono::steady_clock::now() < Deadline) {
        const std::optional<HostEvent> Event = Events.recv_for(200ms);
        if (!Event) {
            continue;
        }
        if (const auto *Failed = std::get_if<HostFailed>(&*Event)) {
            throw Failure(Failed->Reason);
        }
        if (Match(*Event)) {
            return;
        }
    }
    throw Failure("等待宿主事件超时");
}

HostEventSink sink_into(Channel<HostEvent> &Events) {
    return [&Events](HostEvent &&Event) { return Events.try_send(std::move(Event)); };
}

const Registrar CloudScriptInitializes("host.real_cloud_script_initializes_with_isolated_archives", [] {
    const fs::path Scripts = L"D:/Code/MlScriptFolder/script_compile";
    if (!fs::is_regular_file(Scripts / L"main.lua")) {
        throw Skipped("本机没有真实云脚本目录");
    }
    TempDirectory Directory;
    Channel<HostEvent> Events(2048);
    Host Native(host_config(Scripts, Directory.path()), sink_into(Events));
    wait_for_event(Events, [](const HostEvent &Event) { return std::holds_alternative<HostReady>(Event); });
});

const Registrar NativeGlobalRoundTrip("host.native_global_hash_lease_and_broadcast_roundtrip", [] {
    TempDirectory Directory;
    {
        Channel<HostEvent> Events(2048);
        Host Native(host_config(root() / L"tests" / L"scripts" / L"global", Directory.path()), sink_into(Events));
        wait_for_event(Events, [](const HostEvent &Event) {
            const auto *Callback = std::get_if<HostCallback>(&Event);
            if (Callback == nullptr) {
                return false;
            }
            check_equal(Callback->Event["payload"], "global native chain");
            return true;
        });
    }
    // 宿主已关闭，数据库文件可直接读取。
    GlobalArchive Reader(Directory.path());
    const GlobalReply Owner = Reader.request(1, "MsGAHmGet", "hash", R"(["owner"])");
    check_equal(Owner.Result["data"]["value"]["owner"], "玩家一");
});

const Registrar StopHungHostReleasesLock("host.stop_hung_host_releases_native_archive_lock", [] {
    TempDirectory Directory;
    const fs::path Hung = Directory.path() / L"hung";
    fs::create_directories(Hung);
    atomic_write(Hung / L"main.lua", "while true do end");
    const fs::path State = Directory.path() / L"state";
    {
        Channel<HostEvent> Events(2048);
        Host Native(host_config(Hung, State), sink_into(Events));
        std::this_thread::sleep_for(300ms);
        const auto Start = std::chrono::steady_clock::now();
        Native.close();
        check(std::chrono::steady_clock::now() - Start < 5s, "停止挂死的宿主应在数秒内完成");
    }
    // 同目录再开宿主成功，证明旧子进程及其存档锁已被释放。
    Channel<HostEvent> Events(2048);
    Host Again(host_config(root() / L"tests" / L"scripts" / L"counter", State), sink_into(Events));
    wait_for_event(Events, [](const HostEvent &Event) { return std::holds_alternative<HostReady>(Event); });
});

} // namespace
