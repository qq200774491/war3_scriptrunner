#include "Room.h"

#include "Crypto.h"
#include "Error.h"
#include "Socket.h"
#include "Text.h"

#include <algorithm>
#include <format>
#include <future>
#include <list>
#include <map>
#include <mutex>
#include <thread>
#include <variant>

namespace scriptroom {

namespace {

using Millis = std::chrono::milliseconds;
using Seconds = std::chrono::seconds;
using Outbox = Channel<Json>;

constexpr std::size_t MaxLine = 256 * 1024;
constexpr std::size_t MaxClients = 32;
constexpr std::size_t MaxRequestCache = 10000;
constexpr std::size_t MaxRequestPayload = 128 * 1024;
constexpr Seconds HelloTimeout{5};
constexpr Seconds IdleTimeout{3600};

/// 单行 JSON 读取器：限制累计大小，拒绝没有换行的超长包，不会先无限分配再检查。
class LineReader {
public:
    LineReader(Socket &Source, const std::atomic<bool> &Cancel) : Source(Source), Cancel(Cancel) {}

    Json read_json(Millis Timeout) {
        const auto Deadline = std::chrono::steady_clock::now() + Timeout;
        std::string Line;
        while (true) {
            if (Pending.empty()) {
                fill(Deadline);
            }
            const std::size_t Newline = Pending.find('\n');
            const std::size_t Count = Newline == std::string::npos ? Pending.size() : Newline + 1;
            require(Line.size() + Count <= MaxLine, "client frame exceeds limit");
            Line.append(Pending, 0, Count);
            Pending.erase(0, Count);
            if (Line.back() == '\n') {
                break;
            }
        }
        Json Value = parse_json(Line);
        require(Value.is_object(), "expected JSON object");
        return Value;
    }

private:
    void fill(std::chrono::steady_clock::time_point Deadline) {
        char Buffer[16384];
        std::size_t Got = 0;
        const Millis Left = std::max(
            Millis(0), std::chrono::duration_cast<Millis>(Deadline - std::chrono::steady_clock::now()));
        const IoStatus Status = Source.recv_some(Buffer, sizeof(Buffer), Got, Left, Cancel);
        require(Status != IoStatus::Eof, "client disconnected");
        require(Status != IoStatus::Timeout, "deadline has elapsed");
        require(Status != IoStatus::Cancelled, "connection closed");
        Pending.append(Buffer, Got);
    }

    Socket &Source;
    const std::atomic<bool> &Cancel;
    std::string Pending;
};

/// 一条经过校验的客户端请求。
struct ClientRequest {
    std::string Id;
    std::string Event;
    std::string Payload;

    static ClientRequest parse(const Json &Message) {
        require(Message.is_object() && Message.size() == 3, "invalid request content");
        const auto Id = Message.find("id");
        const auto Event = Message.find("event");
        const auto Payload = Message.find("payload");
        require(Id != Message.end() && Event != Message.end() && Payload != Message.end(),
                "invalid request content");
        require(Id->is_string() && Event->is_string() && Payload->is_string(), "invalid request content");
        ClientRequest Request{Id->get<std::string>(), Event->get<std::string>(), Payload->get<std::string>()};
        const std::size_t IdLength = std::count_if(Request.Id.begin(), Request.Id.end(),
                                                    [](char Unit) { return (Unit & 0xC0) != 0x80; });
        require(IdLength >= 1 && IdLength <= 64 && !Request.Event.empty() && Request.Event.size() <= 64 &&
                    Request.Event.front() != '_' && Request.Event.find('\0') == std::string::npos &&
                    Request.Payload.size() <= MaxRequestPayload && Request.Payload.find('\0') == std::string::npos,
                "invalid request content");
        return Request;
    }
};

/// 认证结果，经 promise 从服务线程交还给连接线程。
struct AuthAnswer {
    bool Accepted = false;
    std::size_t Player = 0;
    std::string Reason;
};

struct Hello {
    Json Body;
    std::shared_ptr<Outbox> Box;
    std::promise<AuthAnswer> Answer;
};
struct Left {
    std::size_t Player;
    std::string Reason;
};
struct AcceptFailed {
    std::string Reason;
};
using ServeEvent = std::variant<Hello, Left, AcceptFailed, HostEvent>;

template <typename... Handlers> struct Overloaded : Handlers... {
    using Handlers::operator()...;
};
template <typename... Handlers> Overloaded(Handlers...) -> Overloaded<Handlers...>;

} // namespace

Json Profile::to_json() const {
    return Json{{"version", Version}, {"room", Room}, {"player", Player},
                {"token", Token},     {"host", Host}, {"port", Port}};
}

/// 房间的全部可变状态。
struct Room::Impl {
    /// 一个客户端连接：接收线程和发送线程各一个，任意一端结束都会收拢另一端。
    struct Client {
        Socket Connection;
        std::shared_ptr<Outbox> Box = std::make_shared<Outbox>(256);
        std::atomic<bool> Done{false};
        std::atomic<bool> Reported{false};
        std::atomic<int> Running{2};
        std::atomic<std::int64_t> Player{-1}; ///< 认证通过后的玩家号，未认证为 -1
        std::thread Receiver;
        std::thread Pump;

        void finish() {
            Done.store(true);
            Connection.shutdown_both();
        }
    };

    /// 请求事务：检查、执行、记录构成一次串行事务，重复请求直接返回首次序号。
    class Requests {
    public:
        explicit Requests(const Watch<RoomStatus> &Status) : Active(Status) {}

        void attach(HostSender Sender) {
            std::lock_guard<std::mutex> Lock(SenderMutex);
            Target = std::move(Sender);
        }

        Json request(std::size_t Player, const Json &Message) {
            const ClientRequest Parsed = ClientRequest::parse(Message);
            const Json Content = {{"event", Parsed.Event}, {"payload", Parsed.Payload}};
            const Sha256Digest Fingerprint = sha256(dump_compact(Content));
            const auto Key = std::make_pair(Player, Parsed.Id);

            std::lock_guard<std::mutex> Transaction(CacheMutex);
            const RoomStatus Now = Active.borrow();
            require(Now.Active && !Now.Failure, "room not ready");
            if (const auto Seen = Cache.find(Key); Seen != Cache.end()) {
                require(Seen->second.first == Fingerprint, "request id reused with different content");
                return Json{{"kind", "ack"}, {"id", Parsed.Id}, {"sequence", Seen->second.second}, {"duplicate", true}};
            }
            require(Cache.size() < MaxRequestCache, "room request limit reached; open a new room");
            std::optional<HostSender> Sender;
            {
                std::lock_guard<std::mutex> Lock(SenderMutex);
                Sender = Target;
            }
            require(Sender.has_value(), "room not ready");
            const std::uint64_t Sequence = Sender->event(static_cast<std::int32_t>(Player), Parsed.Event, Parsed.Payload);
            Cache.emplace(Key, std::make_pair(Fingerprint, Sequence));
            return Json{{"kind", "ack"}, {"id", Parsed.Id}, {"sequence", Sequence}, {"duplicate", false}};
        }

    private:
        const Watch<RoomStatus> &Active;
        std::mutex SenderMutex;
        std::optional<HostSender> Target;
        std::mutex CacheMutex;
        std::map<std::pair<std::size_t, std::string>, std::pair<Sha256Digest, std::uint64_t>> Cache;
    };

    Impl(HostConfig Settings, std::optional<std::size_t> RelayTarget, std::shared_ptr<LogChannel> LogSink,
         std::shared_ptr<RunStats> Recorder)
        : Settings(std::move(Settings)), Relay(RelayTarget), Logs(std::move(LogSink)),
          Stats(stats_or_private(std::move(Recorder))), Status(RoomStatus{}), Inbox(1024), Gateway(Status) {}

    ~Impl() { close(); }

    void close();
    void accept_loop();
    void serve();
    void serve_events(RoomStatus &Current);
    void finish_clients(bool Failed);
    void reap_clients();
    void run_receiver(const std::shared_ptr<Client> &Peer);
    void run_pump(const std::shared_ptr<Client> &Peer);
    void leave(const std::shared_ptr<Client> &Peer, const std::string &Reason);
    Json answer(std::size_t Player, const Json &Message);
    std::size_t authenticate(const Json &Body) const;
    void route(Json Event, RoomStatus &Current);
    void send_to(std::size_t Player, Json Message);
    void notify_all(const Json &Message);

    HostConfig Settings;
    std::optional<std::size_t> Relay;
    std::shared_ptr<LogChannel> Logs;
    std::shared_ptr<RunStats> Stats;
    std::array<Profile, 2> Profiles;
    Socket Listener;
    Watch<RoomStatus> Status;
    Channel<ServeEvent> Inbox;
    Requests Gateway;

    std::atomic<bool> Stopping{false};
    std::atomic<bool> Closing{false};
    std::thread Serving;
    std::thread Accepting;

    std::mutex ClientsMutex;
    std::list<std::shared_ptr<Client>> Clients;

    // 以下成员只被服务线程访问。
    std::map<std::size_t, std::shared_ptr<Outbox>> Sessions;
    std::unique_ptr<Host> Native;
};

void Room::Impl::close() {
    Stopping.store(true);
    Inbox.close();
    if (Serving.joinable()) {
        Serving.join();
    }
}

void Room::Impl::accept_loop() {
    try {
        while (!Closing.load() && !Stopping.load()) {
            reap_clients();
            std::optional<Socket> Accepted = Listener.accept_for(Millis(100));
            if (!Accepted) {
                continue;
            }
            Accepted->set_nodelay();
            Accepted->set_send_timeout(Millis(5000));
            // 拒绝多余的未认证连接，不让它们终止已认证的房间。
            std::lock_guard<std::mutex> Lock(ClientsMutex);
            if (Clients.size() >= MaxClients) {
                continue;
            }
            auto Peer = std::make_shared<Client>();
            Peer->Connection = std::move(*Accepted);
            Clients.push_back(Peer);
            Peer->Receiver = std::thread([this, Peer] { run_receiver(Peer); });
            Peer->Pump = std::thread([this, Peer] { run_pump(Peer); });
        }
    } catch (const std::exception &Failure) {
        Inbox.try_send(AcceptFailed{Failure.what()});
    }
}

void Room::Impl::reap_clients() {
    std::list<std::shared_ptr<Client>> Finished;
    {
        std::lock_guard<std::mutex> Lock(ClientsMutex);
        for (auto Item = Clients.begin(); Item != Clients.end();) {
            if ((*Item)->Running.load() == 0) {
                Finished.push_back(std::move(*Item));
                Item = Clients.erase(Item);
            } else {
                ++Item;
            }
        }
    }
    for (const auto &Peer : Finished) {
        Peer->Receiver.join();
        Peer->Pump.join();
    }
}

void Room::Impl::leave(const std::shared_ptr<Client> &Peer, const std::string &Reason) {
    Peer->finish();
    const std::int64_t Player = Peer->Player.load();
    if (Player >= 0 && !Peer->Reported.exchange(true)) {
        Inbox.try_send(Left{static_cast<std::size_t>(Player), Reason});
    }
}

/// 处理一条请求并计时；失败也回一条 error，不断开连接。慢请求的日志只尽力投递，队列满时丢弃。
Json Room::Impl::answer(std::size_t Player, const Json &Message) {
    const auto Began = std::chrono::steady_clock::now();
    Json Response;
    bool Failed = false;
    try {
        Response = Gateway.request(Player, Message);
    } catch (const std::exception &Failure) {
        Failed = true;
        Response = Json{{"kind", "error"}, {"id", Message.contains("id") ? Message["id"] : Json(nullptr)},
                        {"reason", Failure.what()}};
    }
    const auto Elapsed = std::chrono::steady_clock::now() - Began;
    Stats->record_request(Player, Elapsed, Failed);
    if (is_slow_request(Elapsed)) {
        const auto Event = Message.find("event");
        const std::string Name = Event != Message.end() && Event->is_string() ? Event->get<std::string>() : "?";
        Logs->try_send(slow_request_line(Player, Name, Elapsed));
    }
    return Response;
}

void Room::Impl::run_receiver(const std::shared_ptr<Client> &Peer) {
    std::string Reason;
    try {
        LineReader Reader(Peer->Connection, Peer->Done);
        Json Body = Reader.read_json(HelloTimeout);
        std::promise<AuthAnswer> Verdict;
        std::future<AuthAnswer> Answer = Verdict.get_future();
        require(Inbox.send(Hello{std::move(Body), Peer->Box, std::move(Verdict)}), "room closed");
        const AuthAnswer Result = Answer.get();
        if (!Result.Accepted) {
            Peer->Box->send(Json{{"kind", "error"}, {"reason", Result.Reason}});
            std::this_thread::sleep_for(Millis(100));
            throw Error(Result.Reason);
        }
        const std::size_t Identity = Result.Player;
        Peer->Player.store(static_cast<std::int64_t>(Identity));
        while (true) {
            const Json Message = Reader.read_json(IdleTimeout);
            require(Peer->Box->send(answer(Identity, Message)), "connection closed");
        }
    } catch (const std::future_error &) {
        Reason = "room closed";
    } catch (const std::exception &Failure) {
        Reason = Failure.what();
    }
    leave(Peer, Reason);
    Peer->Running.fetch_sub(1);
}

void Room::Impl::run_pump(const std::shared_ptr<Client> &Peer) {
    std::string Reason;
    try {
        while (!Peer->Done.load()) {
            std::optional<Json> Message = Peer->Box->recv_for(Millis(100));
            if (!Message) {
                continue;
            }
            const std::string Line = dump_compact(*Message) + '\n';
            Peer->Connection.send_all(Line.data(), Line.size());
            if (Message->value("kind", "") == "room_failed") {
                break;
            }
        }
    } catch (const std::exception &Failure) {
        Reason = Failure.what();
    }
    leave(Peer, Reason);
    Peer->Running.fetch_sub(1);
}

std::size_t Room::Impl::authenticate(const Json &Body) const {
    const auto PlayerField = Body.find("player");
    require(PlayerField != Body.end() && PlayerField->is_number_unsigned() &&
                PlayerField->get<std::uint64_t>() < 2,
            "invalid player identity");
    const std::size_t Player = PlayerField->get<std::size_t>();
    const Profile &Expected = Profiles[Player];
    const auto Token = Body.find("token");
    require(Token != Body.end() && Token->is_string(), "invalid room credentials");
    const auto Version = Body.find("version");
    const auto RoomId = Body.find("room");
    require(Version != Body.end() && Version->is_number_integer() && Version->get<std::int64_t>() == 1 &&
                RoomId != Body.end() && RoomId->is_string() && RoomId->get<std::string>() == Expected.Room &&
                constant_time_equal(Token->get<std::string>(), Expected.Token),
            "invalid room credentials");
    require(!Sessions.contains(Player), "player already connected");
    return Player;
}

void Room::Impl::send_to(std::size_t Player, Json Message) {
    const auto Session = Sessions.find(Player);
    require(Session != Sessions.end(), "callback target not in connected room");
    require(Session->second->try_send(std::move(Message)), "client outbox full");
}

void Room::Impl::notify_all(const Json &Message) {
    for (const auto &Session : Sessions) {
        Json Copy = Message;
        require(Session.second->try_send(std::move(Copy)), "client outbox full");
    }
}

void Room::Impl::route(Json Event, RoomStatus &Current) {
    require(dump_compact(Event).size() <= MaxLine - 256, "callback exceeds client frame limit");
    const auto Player = Event.find("player");
    require(Player != Event.end() && Player->is_number_integer(), "invalid callback player");
    const std::int64_t Target = Player->get<std::int64_t>();
    require(Target == -1 || (Target >= 0 && Sessions.contains(static_cast<std::size_t>(Target))),
            "callback target not in connected room");
    ++Current.Delivery;
    Stats->record_room_event();
    Event["room"] = Profiles[0].Room;
    Event["delivery"] = Current.Delivery;
    if (Relay) {
        return send_to(*Relay, std::move(Event));
    }
    if (Target != -1) {
        return send_to(static_cast<std::size_t>(Target), std::move(Event));
    }
    notify_all(Event);
}

void Room::Impl::serve_events(RoomStatus &Current) {
    const auto Handlers = Overloaded{
        [&](Hello &Request) {
            std::size_t Player = 0;
            try {
                Player = authenticate(Request.Body);
            } catch (const Error &Rejected) {
                Request.Answer.set_value(AuthAnswer{false, 0, Rejected.what()});
                return;
            }
            require(Request.Box->try_send(Json{{"kind", "joined"}, {"room", Profiles[0].Room}, {"player", Player}}),
                    "client outbox full");
            Sessions.emplace(Player, Request.Box);
            Current.Connected = Sessions.size();
            Status.send_replace(Current);
            Request.Answer.set_value(AuthAnswer{true, Player, {}});
            if (Sessions.size() == 2 && !Native) {
                Native = std::make_unique<Host>(
                    Settings, [this](HostEvent &&Event) { return Inbox.try_send(ServeEvent(std::move(Event))); },
                    Stats);
                Gateway.attach(Native->sender());
            }
        },
        [&](Left &Departure) {
            throw Error(std::format("player {} disconnected; reconnect requires a new room: {}", Departure.Player,
                                    Departure.Reason));
        },
        [&](AcceptFailed &Failure) { throw Error(Failure.Reason); },
        [&](HostEvent &Event) {
            std::visit(Overloaded{
                           [&](HostReady &) {
                               Current.Active = true;
                               Status.send_replace(Current);
                               notify_all(Json{{"kind", "ready"}, {"room", Profiles[0].Room}, {"players", {0, 1}}});
                           },
                           [&](HostLog &Log) { require(Logs->try_send(std::move(Log.Line)), "log queue full"); },
                           [&](HostFailed &Failure) { throw Error(Failure.Reason); },
                           [&](HostCallback &Callback) {
                               route(std::move(Callback.Event), Current);
                               Status.send_replace(Current);
                           },
                       },
                       Event);
        },
    };
    while (!Stopping.load()) {
        std::optional<ServeEvent> Next = Inbox.recv_for(Millis(100));
        if (!Next) {
            if (Inbox.is_closed()) {
                break;
            }
            continue;
        }
        std::visit(Handlers, *Next);
    }
}

void Room::Impl::serve() {
    RoomStatus Current;
    std::optional<std::string> Failure;
    try {
        serve_events(Current);
    } catch (const std::exception &Reason) {
        Failure = Reason.what();
    }
    Closing.store(true);
    // 关闭收件箱并丢弃未处理的事件：销毁未应答的 Hello 会唤醒等待认证的连接线程。
    Inbox.close();
    while (Inbox.try_recv()) {
    }
    Current.Active = false;
    if (Failure) {
        Current.Failure = *Failure;
        for (const auto &Session : Sessions) {
            Session.second->try_send(Json{{"kind", "room_failed"}, {"room", Profiles[0].Room}, {"reason", *Failure}});
        }
        Logs->try_send("[故障] " + *Failure);
    }
    Status.send_replace(Current);
    if (Native) {
        Native->close();
    }
    if (Accepting.joinable()) {
        Accepting.join();
    }
    finish_clients(Failure.has_value());
    Native.reset();
}

void Room::Impl::finish_clients(bool Failed) {
    if (Failed) {
        // 失败消息必须实际写到套接字后才能回收连接。
        const auto Deadline = std::chrono::steady_clock::now() + Seconds(1);
        while (std::chrono::steady_clock::now() < Deadline) {
            {
                std::lock_guard<std::mutex> Lock(ClientsMutex);
                const bool AllDone = std::all_of(Clients.begin(), Clients.end(),
                                                 [](const auto &Peer) { return Peer->Running.load() == 0; });
                if (AllDone) {
                    break;
                }
            }
            std::this_thread::sleep_for(Millis(10));
        }
    }
    std::list<std::shared_ptr<Client>> Remaining;
    {
        std::lock_guard<std::mutex> Lock(ClientsMutex);
        Remaining.swap(Clients);
    }
    for (const auto &Peer : Remaining) {
        Peer->finish();
    }
    for (const auto &Peer : Remaining) {
        Peer->Receiver.join();
        Peer->Pump.join();
    }
}

Room::Room(std::unique_ptr<Impl> Prepared) : State(std::move(Prepared)) {}

std::unique_ptr<Room> Room::start(HostConfig Config, std::optional<std::size_t> Relay,
                                  std::shared_ptr<LogChannel> Logs, std::shared_ptr<RunStats> Stats) {
    require(!Relay || *Relay < 2, "invalid relay");
    require(Logs != nullptr, "missing log channel");
    auto Prepared = std::make_unique<Impl>(std::move(Config), Relay, std::move(Logs), std::move(Stats));
    Prepared->Listener = Socket::listen_loopback();
    const std::uint16_t Port = Prepared->Listener.local_port();
    const std::string RoomId = new_uuid_v4();
    for (std::size_t Player = 0; Player < Prepared->Profiles.size(); ++Player) {
        Profile &Entry = Prepared->Profiles[Player];
        Entry.Version = 1;
        Entry.Room = RoomId;
        Entry.Player = Player;
        Entry.Token = random_hex(32);
        Entry.Host = "127.0.0.1";
        Entry.Port = Port;
    }
    Impl *Raw = Prepared.get();
    Raw->Accepting = std::thread([Raw] { Raw->accept_loop(); });
    Raw->Serving = std::thread([Raw] { Raw->serve(); });
    return std::unique_ptr<Room>(new Room(std::move(Prepared)));
}

Room::~Room() {
    close();
}

const std::array<Profile, 2> &Room::profiles() const {
    return State->Profiles;
}

RoomStatus Room::status() const {
    return State->Status.borrow();
}

bool Room::wait_status_change(std::uint64_t &Seen, Millis Timeout) const {
    return State->Status.wait_changed(Seen, Timeout);
}

void Room::close() {
    if (State) {
        State->close();
    }
}

} // namespace scriptroom
