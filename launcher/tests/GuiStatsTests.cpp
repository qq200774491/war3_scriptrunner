// 运行统计页：时间序列、坐标换算、每秒采样与格式。

#include "StatsBoard.h"
#include "StatsChart.h"
#include "TestHarness.h"

#include <array>

namespace {

using namespace scriptroom;
using namespace scriptroom::gui;
using namespace testing;
using namespace std::chrono_literals;

constexpr ChartArea Plot{100.0f, 50.0f, 200.0f, 100.0f};

const Registrar SeriesDropsOldest("gui.stats.series_ring_buffer_overflow", [] {
    TimeSeries Series(3);
    check(!Series.ago(0).has_value(), "空序列没有点");
    check(!Series.max(3).has_value(), "空序列没有最大值");
    for (const double Value : {1.0, 9.0, 2.0, 3.0}) {
        Series.push(Value);
    }
    check_equal(Series.size(), std::size_t(3));
    check_equal(*Series.ago(0), 3.0);
    check_equal(*Series.ago(2), 9.0); // 最旧的 1 已被挤掉
    check(!Series.ago(3).has_value(), "超出已有点时为空");
    check_equal(*Series.max(3), 9.0);
    check_equal(*Series.max(2), 3.0); // 只看最近两个点
    Series.push(std::nullopt);
    check(!Series.ago(0).has_value(), "空值照样占一格");
    check_equal(*Series.max(2), 3.0);
});

const Registrar CeilingRoundsUp("gui.stats.ceiling_scales_to_nice_numbers", [] {
    TimeSeries Series(60);
    const std::array<const TimeSeries *, 1> One = {&Series};
    check_equal(chart_ceiling(One, 60, 10.0), 10.0); // 没有数据时用下限
    Series.push(0.0);
    check_equal(chart_ceiling(One, 60, 10.0), 10.0);
    Series.push(37.0);
    check_equal(chart_ceiling(One, 60, 10.0), 50.0);
    check_equal(chart_ceiling(One, 60, 10.0, 200.0), 200.0); // 参考线也要放得下
    Series.push(201.0);
    check_equal(chart_ceiling(One, 60, 10.0, 200.0), 500.0);
    Series.push(1.5);
    check_equal(chart_ceiling(One, 1, 1.0), 2.0); // 只看最近一秒
});

const Registrar ChartMapsPoints("gui.stats.chart_coordinates", [] {
    check_equal(chart_y(0.0, 100.0, Plot), 150.0f);
    check_equal(chart_y(100.0, 100.0, Plot), 50.0f);
    check_equal(chart_y(250.0, 100.0, Plot), 50.0f); // 超出量程贴顶
    check_equal(chart_y(-5.0, 100.0, Plot), 150.0f);
    check_equal(chart_x(0, 4, Plot), 275.0f); // 最新一秒在最右边一格
    check_equal(chart_x(3, 4, Plot), 125.0f);
    check_equal(*chart_slot(299.0f, 4, Plot), std::size_t(0));
    check_equal(*chart_slot(100.0f, 4, Plot), std::size_t(3));
    check(!chart_slot(99.0f, 4, Plot).has_value(), "绘图区左边之外");
    check(!chart_slot(300.0f, 4, Plot).has_value(), "绘图区右边之外");
});

const Registrar PolylinesSplitOnGaps("gui.stats.polylines_empty_single_and_gaps", [] {
    TimeSeries Series(10);
    check(chart_polylines(Series, 4, 100.0, Plot).empty(), "空序列不出线");

    Series.push(50.0);
    const auto Single = chart_polylines(Series, 4, 100.0, Plot);
    check_equal(Single.size(), std::size_t(1));
    check_equal(Single[0].size(), std::size_t(1)); // 只有一个点：一段只含一个点，视图画成圆点
    check_equal(Single[0][0].X, 275.0f);
    check_equal(Single[0][0].Y, 100.0f);

    for (const std::optional<double> Value : {std::optional<double>(10.0), std::optional<double>(), std::optional<double>(20.0),
                                              std::optional<double>(30.0)}) {
        Series.push(Value);
    }
    // 窗口 4 秒：只看 10、空、20、30，最旧的 50 不画。
    const auto Lines = chart_polylines(Series, 4, 100.0, Plot);
    check_equal(Lines.size(), std::size_t(2));
    check_equal(Lines[0].size(), std::size_t(1));
    check_equal(Lines[0][0].X, 125.0f);
    check_equal(Lines[1].size(), std::size_t(2));
    check_equal(Lines[1][1].X, 275.0f);
    check_equal(Lines[1][1].Y, 120.0f);
});

const Registrar BarsShareSlots("gui.stats.bars_grouped_per_slot", [] {
    TimeSeries Series(10);
    Series.push(100.0);
    Series.push(0.0);
    Series.push(50.0);
    const auto Bars = chart_bars(Series, 4, 100.0, Plot, 1, 2);
    check_equal(Bars.size(), std::size_t(2)); // 零值不出柱
    // 格宽 50，留 20% 空隙，两组各 20 宽；第二组从 5 + 20 处开始。
    check_equal(Bars[0].X, 275.0f);
    check_equal(Bars[0].W, 20.0f);
    check_equal(Bars[0].Y, 100.0f);
    check_equal(Bars[0].H, 50.0f);
    check(chart_bars(Series, 4, 100.0, Plot, 2, 2).empty(), "组号越界时不出柱");
});

RunStatsSnapshot snapshot(std::uint64_t Requests, double TotalMs, std::uint64_t Events, std::uint64_t Lines,
                          std::size_t Backlog) {
    RunStatsSnapshot Result;
    Result.Players.resize(2);
    Result.Players[0].Latency.Count = Requests;
    Result.Players[0].Latency.TotalMs = TotalMs;
    Result.RoomEvents = Events;
    Result.LogLines = Lines;
    Result.Backlog = Backlog;
    Result.Began = true;
    Result.Running = true;
    Result.Uptime = 1000ms;
    return Result;
}

const Registrar BoardSamplesEverySecond("gui.stats.board_samples_rates_and_latency", [] {
    StatsBoard Board;
    check(!Board.has_data(), "没开始时没有数据");
    const StatsBoard::Clock::time_point Start{};
    check(Board.sample(snapshot(0, 0.0, 0, 0, 0), Start), "第一次总会采样");
    check(Board.has_data(), "云服务启动后有数据");
    check_equal(Board.event_rate().size(), std::size_t(0)); // 第一次只记基准

    check(!Board.sample(snapshot(5, 50.0, 4, 8, 1), Start + 500ms), "不足一秒不采样");
    check(Board.sample(snapshot(5, 50.0, 4, 8, 1), Start + 2s), "满一秒才采样");
    check_equal(*Board.event_rate().ago(0), 2.0);
    check_equal(*Board.log_rate().ago(0), 4.0);
    check_equal(*Board.backlog().ago(0), 1.0);
    check_equal(Board.latency().size(), std::size_t(2));
    check_equal(*Board.latency()[0].ago(0), 10.0);
    check(!Board.latency()[1].ago(0).has_value(), "这一秒没有请求的玩家记为空");

    check(Board.sample(snapshot(7, 110.0, 4, 8, 0), Start + 2500ms, true), "强制采样不看间隔");
    check_equal(*Board.latency()[0].ago(0), 30.0); // 只算这段时间里新增的两次
    check_equal(*Board.event_rate().ago(0), 0.0);

    Board.reset();
    check(!Board.has_data(), "新一轮开始时清空");
    check_equal(Board.latency().size(), std::size_t(0));
});

const Registrar StatsFormatting("gui.stats.formatting", [] {
    check_equal(format_uptime(3723000ms), std::string("01:02:03"));
    check_equal(format_uptime(0ms), std::string("00:00:00"));
    check_equal(format_ms(3.44), std::string("3.4 ms"));
    check_equal(format_ms(350.2), std::string("350 ms"));
    check_equal(format_rate(2.54), std::string("2.5 / 秒"));
});

} // namespace
