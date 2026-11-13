#pragma once

#include "Host.h"
#include "Json.h"
#include "RunStats.h"
#include "Sync.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace scriptroom {

/// 玩家连接房间所需的凭据。
struct Profile {
    std::uint32_t Version = 1;
    std::string Room;
    std::size_t Player = 0;
    std::string Token;
    std::string Host;
    std::uint16_t Port = 0;

    Json to_json() const;
};

/// 房间运行状态，可被其他线程订阅。
struct RoomStatus {
    std::size_t Connected = 0;
    bool Active = false;
    std::uint64_t Delivery = 0;
    std::optional<std::string> Failure;
};

using LogChannel = Channel<std::string>;

/// 两名玩家共享的本地云脚本房间。
///
/// 监听 127.0.0.1 的临时端口，用随机令牌认证固定身份的两名玩家；
/// 两人到齐后启动脚本宿主。玩家断线或宿主故障会使房间失败，需新开房间。
class Room {
public:
    /// 绑定端口、生成凭据并启动服务线程。
    /// @param Relay 若有值，所有回调只发给该玩家（测试用）；否则按事件目标分发。
    /// @param Logs 云脚本日志与故障信息的输出队列；队列满会使房间失败。
    /// @param Stats 记录请求耗时、房间事件与宿主积压，可为空；慢请求另写一行日志到 Logs。
    static std::unique_ptr<Room> start(HostConfig Config, std::optional<std::size_t> Relay,
                                       std::shared_ptr<LogChannel> Logs, std::shared_ptr<RunStats> Stats = nullptr);

    ~Room();
    Room(const Room &) = delete;
    Room &operator=(const Room &) = delete;

    const std::array<Profile, 2> &profiles() const;

    RoomStatus status() const;

    /// 等待状态变化，Seen 为上次观察到的版本号。
    bool wait_status_change(std::uint64_t &Seen, std::chrono::milliseconds Timeout) const;

    /// 停止服务、回收宿主与所有连接。可重复调用。
    void close();

private:
    struct Impl;
    explicit Room(std::unique_ptr<Impl> State);

    std::unique_ptr<Impl> State;
};

} // namespace scriptroom
