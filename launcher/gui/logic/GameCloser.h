#pragma once

#include "GameProcess.h"
#include "Sync.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace scriptroom::gui {

/// 已确认可结束的魔兽进程；确认之前就必须取得，后续不再按 PID 重新查找。
class CloseTarget {
public:
    virtual ~CloseTarget() = default;
    virtual std::uint32_t pid() const = 0;
    /// 结束进程并返回给用户看的结果；失败时抛出 Error。会阻塞，只能在后台线程调用。
    virtual std::string terminate() = 0;
};

/// 关闭魔兽所需的系统能力，生产环境用 SystemGameBackend，测试用替身。
class GameBackend {
public:
    virtual ~GameBackend() = default;
    /// 枚举失败时抛出 Error。
    virtual std::vector<scriptroom::GameProcess> list() = 0;
    /// 发出正常关闭请求，返回结果说明；不抛异常。
    virtual std::string request_close(const scriptroom::GameProcess &Game) = 0;
    /// 为进程取得强制结束句柄；无权限时抛出 Error。
    virtual std::unique_ptr<CloseTarget> capture(const scriptroom::GameProcess &Game) = 0;
};

/// 基于 GameProcess.h 的真实实现。
std::unique_ptr<GameBackend> make_system_game_backend();

/// “关闭全部魔兽”的阶段状态机。四个阶段互斥：
///   Idle -> Waiting（已请求正常关闭，等待 3 秒）
///        -> Confirm（仍有进程，已捕获 ForceTarget，等待用户确认）
///        -> Running（后台线程逐个结束）-> Idle
/// 只在界面线程调用；后台线程只通过内部队列和 Wake 通知界面线程。
class GameCloser {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr std::chrono::seconds GracePeriod{3};

    /// Notify 可从任意线程调用，用于唤醒界面线程执行 poll。
    GameCloser(std::unique_ptr<GameBackend> System, std::function<void()> Notify);
    ~GameCloser();
    GameCloser(const GameCloser &) = delete;
    GameCloser &operator=(const GameCloser &) = delete;

    /// 任何非 Idle 阶段都算忙；忙时不应再次发起。
    bool busy() const;
    bool awaiting_confirmation() const;
    /// 等待宽限期或后台结束线程时，界面需要周期性调用 poll。
    bool needs_polling() const;

    /// 待确认强制结束的进程号；不在确认阶段时为空。
    std::vector<std::uint32_t> pending_pids() const;

    /// 对所有进程发出正常关闭请求，返回每个进程的结果说明；没有进程时只返回提示。
    /// 枚举失败时抛出 Error，状态保持不变。
    std::vector<std::string> request(Clock::time_point Now);

    /// 推进状态机并返回需要展示的消息；Idle 与未到期的 Waiting 不产生消息。
    std::vector<std::string> poll(Clock::time_point Now);

    /// 放弃确认，丢弃已捕获的目标。
    void cancel();

    /// 确认强制结束：把目标交给后台线程逐个结束。不在确认阶段时抛出 Error。
    void terminate();

private:
    /// 后台结束线程发给界面线程的进度：一条结果说明，或结束标记。
    struct Update {
        std::string Message;
        bool Finished = false;
    };
    using UpdateChannel = scriptroom::Channel<Update>;
    struct Idle {};
    struct Waiting {
        Clock::time_point Since;
    };
    struct Confirm {
        std::vector<std::unique_ptr<CloseTarget>> Targets;
    };
    struct Running {
        std::shared_ptr<UpdateChannel> Updates;
        std::thread Worker;
    };

    std::vector<std::string> poll_waiting();
    std::vector<std::string> poll_running(Running &Active);

    /// 后台线程体：逐个结束目标并回报，最后发结束标记并关闭队列。
    static void terminate_all(std::vector<std::unique_ptr<CloseTarget>> Targets, UpdateChannel &Updates,
                              const std::function<void()> &Wake);

    std::unique_ptr<GameBackend> Backend;
    std::function<void()> Wake;
    std::variant<Idle, Waiting, Confirm, Running> Phase;
};

} // namespace scriptroom::gui
