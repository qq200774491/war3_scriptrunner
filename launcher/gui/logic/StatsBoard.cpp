#include "StatsBoard.h"

#include <format>

namespace scriptroom::gui {

namespace {

/// 计数器的增量；统计在两次采样之间被清零过时，增量就是新的读数。
std::uint64_t grown(std::uint64_t After, std::uint64_t Before) {
    return After >= Before ? After - Before : After;
}

} // namespace

void StatsBoard::reset() {
    Latest = RunStatsSnapshot();
    SampledAt.reset();
    Latency.clear();
    Events.clear();
    Lines.clear();
    Backlog.clear();
}

bool StatsBoard::sample(const RunStatsSnapshot &Next, Clock::time_point Now, bool Force) {
    if (SampledAt && Now - *SampledAt < Interval && !Force) {
        return false;
    }
    if (Latency.size() != Next.Players.size()) {
        Latency.assign(Next.Players.size(), TimeSeries(TrendLength));
    }
    if (SampledAt && Now > *SampledAt) {
        const double Seconds = std::chrono::duration<double>(Now - *SampledAt).count();
        for (std::size_t Player = 0; Player < Next.Players.size(); ++Player) {
            const LatencyWindow::Summary &After = Next.Players[Player].Latency;
            const LatencyWindow::Summary Before =
                Player < Latest.Players.size() ? Latest.Players[Player].Latency : LatencyWindow::Summary{};
            const std::uint64_t Count = grown(After.Count, Before.Count);
            const double Spent = Count == After.Count ? After.TotalMs : After.TotalMs - Before.TotalMs;
            Latency[Player].push(Count > 0 ? std::optional<double>(Spent / static_cast<double>(Count)) : std::nullopt);
        }
        Events.push(static_cast<double>(grown(Next.RoomEvents, Latest.RoomEvents)) / Seconds);
        Lines.push(static_cast<double>(grown(Next.LogLines, Latest.LogLines)) / Seconds);
        Backlog.push(static_cast<double>(Next.Backlog));
    }
    Latest = Next;
    SampledAt = Now;
    return true;
}

std::string format_uptime(std::chrono::milliseconds Uptime) {
    const auto Total = std::chrono::duration_cast<std::chrono::seconds>(Uptime).count();
    return std::format("{:02}:{:02}:{:02}", Total / 3600, Total / 60 % 60, Total % 60);
}

std::string format_ms(double Milliseconds) {
    return Milliseconds < 10.0 ? std::format("{:.1f} ms", Milliseconds) : std::format("{:.0f} ms", Milliseconds);
}

std::string format_rate(double PerSecond) {
    return std::format("{:.1f} / 秒", PerSecond);
}

} // namespace scriptroom::gui
