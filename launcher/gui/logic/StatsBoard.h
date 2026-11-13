#pragma once

#include "RunStats.h"
#include "StatsChart.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace scriptroom::gui {

/// 运行统计页的数据：约每秒从后台的统计快照采一次样，算出每秒的平均耗时、事件与日志速率和积压，
/// 存成最近 TrendLength 秒的时间序列。服务停止后不再采样，最后一次的数据保留到下一轮开始。
class StatsBoard {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr std::size_t TrendLength = 60;
    static constexpr std::chrono::seconds Interval{1};

    /// 新一轮开始：清空数据和走势。
    void reset();

    /// 距上次采样不足 Interval 时不采（Force 除外）；返回是否采了。第一次采样只记下基准，不出速率点。
    bool sample(const RunStatsSnapshot &Next, Clock::time_point Now, bool Force = false);

    /// 本轮云服务启动过（运行中或已停止都算）。
    bool has_data() const { return Latest.Running || Latest.Uptime.count() > 0; }

    const RunStatsSnapshot &latest() const { return Latest; }
    /// 每个玩家每秒的平均耗时（毫秒），下标是玩家号；那一秒没有请求时为空。
    const std::vector<TimeSeries> &latency() const { return Latency; }
    const TimeSeries &event_rate() const { return Events; }
    const TimeSeries &log_rate() const { return Lines; }
    const TimeSeries &backlog() const { return Backlog; }

private:
    RunStatsSnapshot Latest;
    std::optional<Clock::time_point> SampledAt;
    std::vector<TimeSeries> Latency;
    TimeSeries Events{TrendLength};
    TimeSeries Lines{TrendLength};
    TimeSeries Backlog{TrendLength};
};

/// “01:02:03”；超过一天也只累计小时。
std::string format_uptime(std::chrono::milliseconds Uptime);
/// 10 毫秒以内保留一位小数：“3.4 ms”“350 ms”。
std::string format_ms(double Milliseconds);
/// “2.5 / 秒”。
std::string format_rate(double PerSecond);

} // namespace scriptroom::gui
