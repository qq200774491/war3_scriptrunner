#include "RunStats.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace scriptroom {

namespace {

double to_ms(std::chrono::nanoseconds Elapsed) {
    return std::chrono::duration<double, std::milli>(Elapsed).count();
}

} // namespace

bool is_slow_request(std::chrono::nanoseconds Elapsed) {
    return Elapsed > SlowRequestLimit;
}

std::string slow_request_line(std::size_t Player, std::string_view Event, std::chrono::nanoseconds Elapsed) {
    return std::format("{} 玩家 {} · {} · {:.0f} ms", SlowRequestTag, Player, Event, to_ms(Elapsed));
}

void LatencyWindow::add(std::chrono::nanoseconds Elapsed) {
    const double Ms = to_ms(Elapsed);
    Recent[Count % Window] = Ms;
    ++Count;
    TotalMs += Ms;
    MaxMs = std::max(MaxMs, Ms);
}

LatencyWindow::Summary LatencyWindow::summary() const {
    Summary Result;
    Result.Count = Count;
    Result.TotalMs = TotalMs;
    Result.MaxMs = MaxMs;
    if (Count == 0) {
        return Result;
    }
    Result.AverageMs = TotalMs / static_cast<double>(Count);
    const std::size_t Held = static_cast<std::size_t>(std::min<std::uint64_t>(Count, Window));
    std::vector<double> Sorted(Recent.begin(), Recent.begin() + static_cast<std::ptrdiff_t>(Held));
    // 最近邻秩：不小于 95% 样本的最小值。
    const std::size_t Rank = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(Held))) - 1;
    std::nth_element(Sorted.begin(), Sorted.begin() + static_cast<std::ptrdiff_t>(Rank), Sorted.end());
    Result.P95Ms = Sorted[Rank];
    return Result;
}

void RunStats::begin(std::size_t Players) {
    for (PlayerSlot &Slot : Slots) {
        std::lock_guard<std::mutex> Lock(Slot.Mutex);
        Slot.Latency = LatencyWindow();
        Slot.Failures = 0;
    }
    ActivePlayers.store(std::min(Players, MaxPlayers));
    RoomEvents.store(0);
    LogLines.store(0);
    Backlog.store(0);
    PeakBacklog.store(0);
    std::lock_guard<std::mutex> Lock(TimeMutex);
    Started = Clock::now();
    Stopped.reset();
}

void RunStats::end() {
    std::lock_guard<std::mutex> Lock(TimeMutex);
    if (Started && !Stopped) {
        Stopped = Clock::now();
    }
}

void RunStats::record_request(std::size_t Player, std::chrono::nanoseconds Elapsed, bool Failed) {
    if (Player >= ActivePlayers.load(std::memory_order_relaxed)) {
        return;
    }
    PlayerSlot &Slot = Slots[Player];
    std::lock_guard<std::mutex> Lock(Slot.Mutex);
    Slot.Latency.add(Elapsed);
    Slot.Failures += Failed ? 1 : 0;
}

void RunStats::record_room_event() {
    RoomEvents.fetch_add(1, std::memory_order_relaxed);
}

void RunStats::record_log_lines(std::uint64_t Count) {
    LogLines.fetch_add(Count, std::memory_order_relaxed);
}

void RunStats::record_backlog(std::size_t Depth) {
    Backlog.store(Depth, std::memory_order_relaxed);
    std::size_t Peak = PeakBacklog.load(std::memory_order_relaxed);
    while (Depth > Peak && !PeakBacklog.compare_exchange_weak(Peak, Depth, std::memory_order_relaxed)) {
    }
}

RunStatsSnapshot RunStats::snapshot() const {
    RunStatsSnapshot Result;
    Result.Players.resize(ActivePlayers.load(std::memory_order_relaxed));
    for (std::size_t Player = 0; Player < Result.Players.size(); ++Player) {
        std::lock_guard<std::mutex> Lock(Slots[Player].Mutex);
        Result.Players[Player].Latency = Slots[Player].Latency.summary();
        Result.Players[Player].Failures = Slots[Player].Failures;
    }
    Result.RoomEvents = RoomEvents.load(std::memory_order_relaxed);
    Result.LogLines = LogLines.load(std::memory_order_relaxed);
    Result.Backlog = Backlog.load(std::memory_order_relaxed);
    Result.PeakBacklog = PeakBacklog.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> Lock(TimeMutex);
    if (Started) {
        const Clock::time_point Until = Stopped ? *Stopped : Clock::now();
        Result.Uptime = std::chrono::duration_cast<std::chrono::milliseconds>(Until - *Started);
        Result.Running = !Stopped.has_value();
    }
    return Result;
}

std::shared_ptr<RunStats> stats_or_private(std::shared_ptr<RunStats> Stats) {
    return Stats ? std::move(Stats) : std::make_shared<RunStats>();
}

} // namespace scriptroom
