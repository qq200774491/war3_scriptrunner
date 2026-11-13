#include "Host.h"

#include "Error.h"
#include "GlobalArchive.h"
#include "ChildProcess.h"
#include "Text.h"
#include "Wire.h"

#include <deque>
#include <format>
#include <future>
#include <mutex>
#include <optional>

namespace scriptroom {

namespace {

using Clock = std::chrono::steady_clock;
using Millis = std::chrono::milliseconds;
using Seconds = std::chrono::seconds;

constexpr std::size_t MaxQueuedFrames = 256;
constexpr std::size_t MaxQueuedGlobals = 256;
constexpr Millis TickInterval{250};
constexpr Seconds StartupLimit{20};
constexpr Seconds HeartbeatLimit{20};
constexpr Seconds RequestLimit{20};
constexpr Millis WriteLimit{5000};
constexpr std::uint8_t EventKind = 0x10;
constexpr std::uint8_t ShutdownKind = 0x11;
constexpr std::uint8_t GlobalReplyKind = 0x12;

} // namespace

/// 宿主线程与外部共享的收件箱。互斥量同时保护命令队列、帧队列和关闭标志。
struct HostInbox {
    struct Request {
        std::int32_t Player = 0;
        std::string Name;
        std::string Payload;
        std::promise<std::uint64_t> Reply;
    };

    /// 读线程产出的帧；解码或读取失败时只带错误文本，之后读线程结束。
    struct Frame {
        std::optional<wire::Message> Body;
        std::string Failure;
    };

    std::mutex Mutex;
    std::condition_variable Wake;
    std::deque<Request> Commands;
    std::deque<Frame> Frames;
    bool Stop = false;       ///< 外部请求停止
    bool Closed = false;     ///< 宿主线程已退出，不再接收命令
    bool ReaderQuit = false; ///< 通知读线程不要再等待空位
};

std::uint64_t HostSender::event(std::int32_t Player, std::string Name, std::string Payload) const {
    HostInbox::Request Pending;
    Pending.Player = Player;
    Pending.Name = std::move(Name);
    Pending.Payload = std::move(Payload);
    std::future<std::uint64_t> Answer = Pending.Reply.get_future();
    {
        std::lock_guard<std::mutex> Lock(Shared->Mutex);
        require(!Shared->Closed, "host closed");
        Shared->Commands.push_back(std::move(Pending));
    }
    Shared->Wake.notify_all();
    try {
        return Answer.get();
    } catch (const std::future_error &) {
        throw Error("host stopped before acknowledgement");
    }
}

namespace {

/// 宿主线程内部状态：就绪标志、未确认请求、待回传的全局存档应答。
class Session {
public:
    Session(const HostConfig &Settings, const HostEventSink &Sink, ChildProcess &Child)
        : Settings(Settings), Sink(Sink), Child(Child), Heartbeat(Clock::now()) {}

    bool is_ready() const { return Ready; }
    bool is_idle() const { return !Pending.has_value(); }
    bool has_global_reply() const { return !Globals.empty(); }

    void send_global_reply() {
        auto [Name, Payload] = std::move(Globals.front());
        Globals.pop_front();
        send(GlobalReplyKind, -1, Name, Payload, std::nullopt);
    }

    void send_command(HostInbox::Request &&Command) {
        send(EventKind, Command.Player, Command.Name, Command.Payload, std::move(Command.Reply));
    }

    void handle(const wire::Message &Message) { std::visit([this](const auto &Body) { on(Body); }, Message); }

    /// 周期检查：启动、心跳和请求都有 20 秒上限。
    void check_deadlines(Clock::time_point Start) const {
        const Clock::time_point Now = Clock::now();
        require(Ready || Now - Start < StartupLimit, "host startup timed out");
        require(!Ready || Now - Heartbeat < HeartbeatLimit, "host heartbeat timed out");
        require(!Pending || Now - Pending->Since < RequestLimit, "host request timed out");
    }

private:
    struct PendingRequest {
        std::uint64_t Sequence = 0;
        Clock::time_point Since;
        std::optional<std::promise<std::uint64_t>> Reply;
    };

    void send(std::uint8_t Kind, std::int32_t Player, const std::string &Name, const std::string &Payload,
              std::optional<std::promise<std::uint64_t>> Reply) {
        ++Sequence;
        const std::string Bytes = wire::encode_event(Kind, Sequence, Player, Name, Payload);
        wire::write_frame(Child.stdin_pipe(), Bytes, WriteLimit);
        Pending = PendingRequest{Sequence, Clock::now(), std::move(Reply)};
    }

    void publish(HostEvent &&Event) {
        require(Sink(std::move(Event)), "host event queue full");
    }

    void on(const wire::LogMessage &Log) {
        publish(HostLog{std::format("[云脚本 {}] {}", Log.Level, Log.Text)});
    }

    void on(const wire::EventMessage &Event) {
        Json Body = {{"kind", "event"}, {"player", Event.Player}, {"event", Event.Name}};
        if (is_valid_utf8(Event.Payload)) {
            Body["payload"] = Event.Payload;
        } else {
            Body["payload"] = hex_encode(std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t *>(Event.Payload.data()), Event.Payload.size()));
            Body["payload_encoding"] = "hex";
        }
        publish(HostCallback{std::move(Body)});
    }

    void on(const wire::ReadyMessage &) {
        require(!Ready, "duplicate host readiness");
        Ready = true;
        Heartbeat = Clock::now();
        publish(HostReady{});
    }

    void on(const wire::AckMessage &Ack) {
        require(Pending.has_value(), "unexpected host acknowledgement");
        PendingRequest Done = std::move(*Pending);
        Pending.reset();
        require(Done.Sequence == Ack.Sequence, "host acknowledgement sequence mismatch");
        if (Done.Reply) {
            Done.Reply->set_value(Ack.Sequence);
        }
    }

    void on(const wire::FailureMessage &Failure) { throw Error(Failure.Reason); }

    void on(const wire::HeartbeatMessage &) { Heartbeat = Clock::now(); }

    void on(const wire::GlobalMessage &Request) {
        require(Globals.size() < MaxQueuedGlobals, "global request queue full");
        if (!Archive) {
            std::error_code Ignored;
            std::filesystem::create_directories(Settings.Archives, Ignored);
            Archive.emplace(Settings.Archives);
        }
        GlobalReply Reply = Archive->request(Request.Transaction, Request.Operation, Request.Key, Request.Argument);
        Globals.emplace_back(std::move(Reply.EventName), dump_compact(Reply.Result));
    }

    const HostConfig &Settings;
    const HostEventSink &Sink;
    ChildProcess &Child;
    std::optional<PendingRequest> Pending;
    std::deque<std::pair<std::string, std::string>> Globals;
    std::optional<GlobalArchive> Archive;
    std::uint64_t Sequence = 0;
    bool Ready = false;
    Clock::time_point Heartbeat;
};

/// 读线程：把宿主 stdout 的帧搬进收件箱；首个错误后结束。
void read_frames(PipeStream &Output, HostInbox &Box) {
    while (true) {
        HostInbox::Frame Item;
        try {
            Item.Body = wire::read_frame(Output);
        } catch (const std::exception &Failure) {
            Item.Failure = Failure.what();
        }
        const bool Failed = !Item.Body.has_value();
        {
            std::unique_lock<std::mutex> Lock(Box.Mutex);
            Box.Wake.wait(Lock, [&] { return Box.ReaderQuit || Box.Frames.size() < MaxQueuedFrames; });
            if (Box.ReaderQuit) {
                return;
            }
            Box.Frames.push_back(std::move(Item));
        }
        Box.Wake.notify_all();
        if (Failed) {
            return;
        }
    }
}

/// 请求宿主优雅退出，1 秒内未退出则强制结束。
void shutdown(ChildProcess &Child) {
    try {
        wire::write_frame(Child.stdin_pipe(), std::string(1, static_cast<char>(ShutdownKind)), Millis(500));
    } catch (const std::exception &) {
        // 宿主可能已经挂死或退出；下面的强制回收兜底。
    }
    if (!Child.wait_for(Seconds(1))) {
        Child.kill();
        Child.wait_for(Seconds(2));
    }
}

} // namespace

Host::Host(HostConfig Settings, HostEventSink EventSink)
    : Config(std::move(Settings)), Sink(std::move(EventSink)), Shared(std::make_shared<HostInbox>()) {
    Worker = std::thread([this] { run(); });
}

Host::~Host() {
    close();
}

HostSender Host::sender() const {
    return HostSender(Shared);
}

void Host::close() {
    {
        std::lock_guard<std::mutex> Lock(Shared->Mutex);
        Shared->Stop = true;
    }
    Shared->Wake.notify_all();
    if (Worker.joinable()) {
        Worker.join();
    }
}

void Host::run() {
    std::string Failure;
    try {
        ChildSpec Spec;
        Spec.Executable = Config.Executable;
        Spec.Arguments = {path_to_utf8(Config.Dll), path_to_utf8(Config.Scripts), "0,1",
                          path_to_utf8(Config.Archives)};
        Spec.PipeStdin = true;
        Spec.PipeStdout = true;
        Spec.KillOnDestroy = true;
        ChildProcess Child = [&] {
            try {
                return ChildProcess::spawn(Spec);
            } catch (const Error &Reason) {
                throw Error(std::format("启动 script_host.exe: {}", Reason.what()));
            }
        }();

        HostInbox &Box = *Shared;
        std::thread Reader([&] { read_frames(Child.stdout_pipe(), Box); });
        std::exception_ptr Pending;
        try {
            Session State(Config, Sink, Child);
            const Clock::time_point Start = Clock::now();
            Clock::time_point NextTick = Start + TickInterval;
            while (true) {
                {
                    std::lock_guard<std::mutex> Lock(Box.Mutex);
                    if (Box.Stop) {
                        break;
                    }
                }
                if (State.is_ready() && State.is_idle() && State.has_global_reply()) {
                    State.send_global_reply();
                }
                std::optional<HostInbox::Frame> Frame;
                std::optional<HostInbox::Request> Command;
                bool TickDue = false;
                {
                    std::unique_lock<std::mutex> Lock(Box.Mutex);
                    Box.Wake.wait_until(Lock, NextTick, [&] {
                        return Box.Stop || !Box.Frames.empty() ||
                               (State.is_ready() && State.is_idle() && !Box.Commands.empty());
                    });
                    if (Box.Stop) {
                        break;
                    }
                    if (!Box.Frames.empty()) {
                        Frame = std::move(Box.Frames.front());
                        Box.Frames.pop_front();
                    } else if (State.is_ready() && State.is_idle() && !Box.Commands.empty()) {
                        Command = std::move(Box.Commands.front());
                        Box.Commands.pop_front();
                    } else if (Clock::now() >= NextTick) {
                        TickDue = true;
                    }
                }
                if (Frame) {
                    Box.Wake.notify_all(); // 读线程可能在等空位
                    require(Frame->Body.has_value(), Frame->Failure);
                    State.handle(*Frame->Body);
                } else if (Command) {
                    State.send_command(std::move(*Command));
                } else if (TickDue) {
                    State.check_deadlines(Start);
                    NextTick += TickInterval;
                }
            }
        } catch (...) {
            Pending = std::current_exception();
        }
        // 所有退出路径都回收子进程；停止不依赖调用方是否仍在接收事件。
        shutdown(Child);
        {
            std::lock_guard<std::mutex> Lock(Box.Mutex);
            Box.ReaderQuit = true;
        }
        Box.Wake.notify_all();
        Child.stdout_pipe().cancel();
        Reader.join();
        if (Pending) {
            std::rethrow_exception(Pending);
        }
    } catch (const std::exception &Reason) {
        Failure = Reason.what();
    }

    {
        std::lock_guard<std::mutex> Lock(Shared->Mutex);
        Shared->Closed = true;
        Shared->Commands.clear(); // 未发送的请求随 promise 销毁而失败
        Shared->Frames.clear();
    }
    if (Failure.empty()) {
        return;
    }
    // 停止期间不再汇报故障；否则阻塞到事件被接收或收到停止请求。
    std::unique_lock<std::mutex> Lock(Shared->Mutex);
    while (!Shared->Stop) {
        Lock.unlock();
        const bool Delivered = Sink(HostFailed{Failure});
        Lock.lock();
        if (Delivered) {
            break;
        }
        Shared->Wake.wait_for(Lock, Millis(20), [&] { return Shared->Stop; });
    }
}

} // namespace scriptroom
