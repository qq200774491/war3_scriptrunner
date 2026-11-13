#pragma once

#include "Json.h"
#include "RunStats.h"
#include "Sync.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <variant>

namespace scriptroom {

namespace fs = std::filesystem;

/// 32 位脚本宿主进程及其资源。
struct HostConfig {
    fs::path Executable; ///< script_host.exe
    fs::path Dll;        ///< scriptrunner.dll
    fs::path Scripts;    ///< 含 main.lua 的云脚本目录
    fs::path Archives;   ///< 存档根目录
};

/// 宿主向外汇报的事件。
struct HostReady {};
struct HostLog {
    std::string Line;
};
struct HostCallback {
    Json Event; ///< {"kind":"event","player":..,"event":..,"payload":..}
};
struct HostFailed {
    std::string Reason;
};
using HostEvent = std::variant<HostReady, HostLog, HostCallback, HostFailed>;

/// 事件接收函数；队列已满时返回 false 且不消耗事件。
using HostEventSink = std::function<bool(HostEvent &&)>;

class Host;
struct HostInbox;

/// 向宿主提交事件的可复制句柄；调用线程阻塞到宿主确认。
class HostSender {
public:
    /// 投递一个事件并等待宿主 ACK，返回事件序号。宿主关闭或停止前未确认时抛出 Error。
    std::uint64_t event(std::int32_t Player, std::string Name, std::string Payload) const;

private:
    friend class Host;
    explicit HostSender(std::shared_ptr<HostInbox> Shared) : Shared(std::move(Shared)) {}

    std::shared_ptr<HostInbox> Shared;
};

/// 脚本宿主进程管理器：一个独立线程独占宿主 stdin，
/// 读取、心跳和全局回包在等待 ACK 时仍能推进。
class Host {
public:
    /// 启动线程与子进程；启动失败通过 HostFailed 事件汇报。Stats 记录等待送进宿主的请求积压，可为空。
    Host(HostConfig Config, HostEventSink Sink, std::shared_ptr<RunStats> Stats = nullptr);
    ~Host();
    Host(const Host &) = delete;
    Host &operator=(const Host &) = delete;

    HostSender sender() const;

    /// 请求停止并等待线程与子进程完全退出。可重复调用。
    void close();

private:
    void run();

    HostConfig Config;
    HostEventSink Sink;
    std::shared_ptr<HostInbox> Shared;
    std::thread Worker;
};

} // namespace scriptroom
