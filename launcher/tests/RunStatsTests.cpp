// 运行统计：耗时汇总、环形缓冲、积压峰值、慢请求判定。

#include "RunStats.h"
#include "TestHarness.h"

#include <thread>
#include <vector>

namespace {

using namespace scriptroom;
using namespace testing;
using namespace std::chrono_literals;

const Registrar LatencyAverageMaxP95("stats.latency_average_max_p95", [] {
    LatencyWindow Window;
    check_equal(Window.summary().Count, std::uint64_t(0));
    check_equal(Window.summary().P95Ms, 0.0);
    for (int Ms = 1; Ms <= 100; ++Ms) {
        Window.add(std::chrono::milliseconds(Ms));
    }
    const LatencyWindow::Summary Result = Window.summary();
    check_equal(Result.Count, std::uint64_t(100));
    check_equal(Result.AverageMs, 50.5);
    check_equal(Result.MaxMs, 100.0);
    check_equal(Result.P95Ms, 95.0);
    check_equal(Result.TotalMs, 5050.0);
});

const Registrar LatencyRingOverwritesOldest("stats.latency_ring_buffer_overflow", [] {
    LatencyWindow Window;
    Window.add(10s); // 很快会被挤出窗口，但仍计入次数、平均和最大值
    for (std::size_t Index = 0; Index < LatencyWindow::Window; ++Index) {
        Window.add(1ms);
    }
    const LatencyWindow::Summary Result = Window.summary();
    check_equal(Result.Count, std::uint64_t(LatencyWindow::Window + 1));
    check_equal(Result.MaxMs, 10000.0);
    check_equal(Result.P95Ms, 1.0);
    check(Result.AverageMs > 1.0, "平均值应包含被挤出窗口的样本");
});

const Registrar StatsSnapshotAndReset("stats.snapshot_counts_and_begin_resets", [] {
    RunStats Stats;
    check(!Stats.snapshot().Running, "begin 之前不算运行中");
    check(Stats.snapshot().Players.empty(), "begin 之前没有玩家");
    Stats.begin(2);
    Stats.record_request(0, 20ms, false);
    Stats.record_request(0, 40ms, true);
    Stats.record_request(1, 5ms, false);
    Stats.record_request(2, 5ms, false); // 超出本轮玩家数的请求被忽略
    Stats.record_room_event();
    Stats.record_room_event();
    Stats.record_log_lines(3);
    Stats.record_backlog(4);
    Stats.record_backlog(1);

    RunStatsSnapshot Now = Stats.snapshot();
    check(Now.Running, "begin 之后应在运行");
    check_equal(Now.Players.size(), std::size_t(2));
    check_equal(Now.Players[0].Latency.Count, std::uint64_t(2));
    check_equal(Now.Players[0].Latency.AverageMs, 30.0);
    check_equal(Now.Players[0].Failures, std::uint64_t(1));
    check_equal(Now.Players[1].Latency.Count, std::uint64_t(1));
    check_equal(Now.RoomEvents, std::uint64_t(2));
    check_equal(Now.LogLines, std::uint64_t(3));
    check_equal(Now.Backlog, std::size_t(1));
    check_equal(Now.PeakBacklog, std::size_t(4));

    Stats.end();
    const RunStatsSnapshot Stopped = Stats.snapshot();
    check(!Stopped.Running, "end 之后不在运行");
    check_equal(Stopped.Players[0].Latency.Count, std::uint64_t(2)); // 停止后保留数据
    std::this_thread::sleep_for(20ms);
    check_equal(Stats.snapshot().Uptime, Stopped.Uptime); // 运行时长停在 end 的时刻

    Stats.begin(4);
    Now = Stats.snapshot();
    check_equal(Now.Players.size(), std::size_t(4));
    check_equal(Now.Players[0].Latency.Count, std::uint64_t(0));
    check_equal(Now.PeakBacklog, std::size_t(0));
    check_equal(Now.LogLines, std::uint64_t(0));
});

const Registrar StatsAreThreadSafe("stats.concurrent_recording", [] {
    RunStats Stats;
    Stats.begin(2);
    std::vector<std::thread> Workers;
    for (std::size_t Thread = 0; Thread < 4; ++Thread) {
        Workers.emplace_back([&Stats, Thread] {
            for (std::size_t Index = 0; Index < 1000; ++Index) {
                Stats.record_request(Thread % 2, 1ms, false);
                Stats.record_room_event();
                Stats.record_backlog(Index % 8);
            }
        });
    }
    for (std::thread &Worker : Workers) {
        Worker.join();
    }
    const RunStatsSnapshot Now = Stats.snapshot();
    check_equal(Now.Players[0].Latency.Count + Now.Players[1].Latency.Count, std::uint64_t(4000));
    check_equal(Now.RoomEvents, std::uint64_t(4000));
    check_equal(Now.PeakBacklog, std::size_t(7));
});

const Registrar SlowRequestThreshold("stats.slow_request_threshold_and_line", [] {
    check(!is_slow_request(SlowRequestLimit), "等于阈值不算慢");
    check(is_slow_request(SlowRequestLimit + 1ms), "超过阈值算慢");
    check(!is_slow_request(5ms), "快请求不算慢");
    check_equal(slow_request_line(1, "increment", 350ms), std::string("[慢请求] 玩家 1 · increment · 350 ms"));
});

} // namespace
