#pragma once

#include "Config.h"
#include "Room.h"
#include "RunStats.h"
#include "Sync.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace scriptroom::gui {

/// “启动”跑完整一轮；“检查”只验证配置与组件。
enum class SessionMode { Run, Check };

/// 后台线程里执行的整个任务。抛出的异常即任务失败原因。任务把这一轮的运行统计记进第三个参数。
using SessionTask = std::function<void(StopHandle, std::shared_ptr<LogChannel>, std::shared_ptr<RunStats>)>;

/// 真实任务：Check 调用 check_setup，Run 调用 run_session。
SessionTask make_session_task(const fs::path &Root, Config Settings, SessionMode Mode);

/// 在后台线程运行一个任务，并把日志转交给界面线程。
///
/// 线程契约：构造、take_logs、finished、stop 和析构都在界面线程；
/// 后台线程只会调用 Notify 回调（可从任意线程调用，用来唤醒界面线程）。
/// 完成状态在所有日志转交之后才可见：先读 finished 再 take_logs，不会丢行。
class SessionWorker {
public:
    /// 任务结束时的结果；Failure 为空表示正常结束。
    struct Outcome {
        std::optional<std::string> Failure;
    };

    SessionWorker(SessionTask Task, std::function<void()> Notify);
    ~SessionWorker();
    SessionWorker(const SessionWorker &) = delete;
    SessionWorker &operator=(const SessionWorker &) = delete;

    /// 请求任务停止；可重复调用。
    void stop();

    /// 取走已到达的日志行。
    std::vector<std::string> take_logs();

    /// 任务已结束时返回结果，否则返回空。
    std::optional<Outcome> finished();

    /// 这一轮运行统计的快照；任务运行中和结束后都可以读。
    RunStatsSnapshot stats() const { return Stats->snapshot(); }

private:
    void run_task(const SessionTask &Task);
    void relay_logs();
    void push_line(std::string Line);

    StopHandle Stop = std::make_shared<StopSignal>();
    std::shared_ptr<LogChannel> Logs = std::make_shared<LogChannel>(2048);
    std::shared_ptr<RunStats> Stats = std::make_shared<RunStats>();
    std::function<void()> Wake;

    std::mutex Mutex;
    std::vector<std::string> Pending; ///< 受 Mutex 保护
    std::optional<Outcome> Result;    ///< 受 Mutex 保护

    std::thread Relay;
    std::thread Runner;
};

} // namespace scriptroom::gui
