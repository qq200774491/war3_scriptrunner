#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace scriptroom {

/// 耗时超过它的房间请求算慢请求，会写一行以 SlowRequestTag 开头的日志。
inline constexpr std::chrono::milliseconds SlowRequestLimit{200};
inline constexpr std::string_view SlowRequestTag = "[慢请求]";

bool is_slow_request(std::chrono::nanoseconds Elapsed);

/// 慢请求的日志行：“[慢请求] 玩家 0 · increment · 350 ms”。
std::string slow_request_line(std::size_t Player, std::string_view Event, std::chrono::nanoseconds Elapsed);

/// 一组耗时的汇总：次数、总和与最大值覆盖全部样本，P95 只看最近 Window 次。
class LatencyWindow {
public:
    static constexpr std::size_t Window = 512;

    struct Summary {
        std::uint64_t Count = 0;
        double TotalMs = 0.0;
        double AverageMs = 0.0;
        double MaxMs = 0.0;
        double P95Ms = 0.0;
    };

    void add(std::chrono::nanoseconds Elapsed);
    Summary summary() const;

private:
    std::array<double, Window> Recent{}; ///< 环形缓冲，写满后覆盖最旧的样本
    std::uint64_t Count = 0;
    double TotalMs = 0.0;
    double MaxMs = 0.0;
};

struct PlayerStats {
    LatencyWindow::Summary Latency;
    std::uint64_t Failures = 0;
};

struct RunStatsSnapshot {
    std::vector<PlayerStats> Players; ///< 下标是玩家号
    std::uint64_t RoomEvents = 0;
    std::uint64_t LogLines = 0;
    std::size_t Backlog = 0;     ///< 等待送进脚本宿主的请求数
    std::size_t PeakBacklog = 0;
    std::chrono::milliseconds Uptime{0};
    bool Running = false;
};

/// 一轮运行的统计。所有方法都可以从任意线程调用；记录只做原子加或很短的加锁，不会拖慢请求。
class RunStats {
public:
    using Clock = std::chrono::steady_clock;

    /// 一局魔兽最多 12 名玩家。
    static constexpr std::size_t MaxPlayers = 12;

    /// 新一轮开始：清零、按玩家数开出统计槽并开始计时。之后玩家号不小于 Players 的请求不计入。
    void begin(std::size_t Players);
    /// 停止计时；数据保留到下一次 begin。
    void end();

    void record_request(std::size_t Player, std::chrono::nanoseconds Elapsed, bool Failed);
    void record_room_event();
    void record_log_lines(std::uint64_t Count);
    void record_backlog(std::size_t Depth);

    RunStatsSnapshot snapshot() const;

private:
    struct PlayerSlot {
        mutable std::mutex Mutex;
        LatencyWindow Latency;
        std::uint64_t Failures = 0;
    };

    std::array<PlayerSlot, MaxPlayers> Slots;
    std::atomic<std::size_t> ActivePlayers{0};
    std::atomic<std::uint64_t> RoomEvents{0};
    std::atomic<std::uint64_t> LogLines{0};
    std::atomic<std::size_t> Backlog{0};
    std::atomic<std::size_t> PeakBacklog{0};

    mutable std::mutex TimeMutex;
    std::optional<Clock::time_point> Started;
    std::optional<Clock::time_point> Stopped;
};

/// 调用方没有给统计对象时用一个私有的，记录处就不必处处判空。
std::shared_ptr<RunStats> stats_or_private(std::shared_ptr<RunStats> Stats);

} // namespace scriptroom
