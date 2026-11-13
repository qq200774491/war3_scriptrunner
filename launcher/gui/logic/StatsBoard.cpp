#include "StatsBoard.h"

#include <algorithm>
#include <cmath>
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

std::size_t range_seconds(TrendRange Range) {
    switch (Range) {
    case TrendRange::HalfMinute:
        return 30;
    case TrendRange::FiveMinutes:
        return 300;
    case TrendRange::Minute:
        break;
    }
    return 60;
}

const char *range_label(TrendRange Range) {
    switch (Range) {
    case TrendRange::HalfMinute:
        return "30 秒";
    case TrendRange::FiveMinutes:
        return "5 分钟";
    case TrendRange::Minute:
        break;
    }
    return "60 秒";
}

StatsBoard sample_stats_board() {
    StatsBoard Board;
    RunStatsSnapshot Now;
    Now.Players.resize(2);
    Now.Began = true;
    Now.Running = true;
    const StatsBoard::Clock::time_point Start{};
    for (int Second = 0; Second <= 130; ++Second) {
        const double Wave = std::sin(Second * 0.21);
        const std::array<double, 2> Spent = {32.0 + 14.0 * Wave + (Second == 96 ? 230.0 : 0.0),
                                             48.0 + 20.0 * std::cos(Second * 0.17)};
        for (std::size_t Player = 0; Player < 2; ++Player) {
            LatencyWindow::Summary &Latency = Now.Players[Player].Latency;
            const std::uint64_t Requests = Player == 0 ? 3 : 2;
            Latency.Count += Requests;
            Latency.TotalMs += Spent[Player] * static_cast<double>(Requests);
            Latency.MaxMs = std::max(Latency.MaxMs, Spent[Player]);
            Latency.AverageMs = Latency.TotalMs / static_cast<double>(Latency.Count);
            Latency.P95Ms = Player == 0 ? 58.0 : 71.0;
        }
        Now.Players[1].Failures = Second > 60 ? 1 : 0;
        Now.RoomEvents += static_cast<std::uint64_t>(5.0 + 3.0 * Wave);
        Now.LogLines += static_cast<std::uint64_t>(9.0 + 6.0 * std::cos(Second * 0.13));
        Now.Backlog = static_cast<std::size_t>(Second % 17 == 0 ? 4 : (Second % 5 == 0 ? 1 : 0));
        Now.PeakBacklog = std::max(Now.PeakBacklog, Now.Backlog);
        Now.Uptime = std::chrono::seconds(Second);
        Board.sample(Now, Start + std::chrono::seconds(Second));
    }
    return Board;
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
