#include "SessionWorker.h"

#include "Launch.h"

#include <utility>

namespace scriptroom::gui {

SessionTask make_session_task(const fs::path &Root, Config Settings, SessionMode Mode) {
    if (Mode == SessionMode::Check) {
        return [Root, Settings = std::move(Settings)](StopHandle, std::shared_ptr<LogChannel> Logs) {
            check_setup(Root, Settings);
            Logs->try_send("[检查] 配置及组件通过；启动时检查端口与存档锁。");
        };
    }
    return [Root, Settings = std::move(Settings)](StopHandle Stop, std::shared_ptr<LogChannel> Logs) {
        run_session(Root, Settings, std::move(Stop), std::move(Logs));
    };
}

SessionWorker::SessionWorker(SessionTask Task, std::function<void()> Notify) : Wake(std::move(Notify)) {
    // 先起转交线程：任务线程结束时会关闭队列并等它排空。
    Relay = std::thread([this] { relay_logs(); });
    Runner = std::thread([this, Body = std::move(Task)] { run_task(Body); });
}

SessionWorker::~SessionWorker() {
    stop();
    Runner.join();
    if (Relay.joinable()) {
        Relay.join();
    }
}

void SessionWorker::stop() {
    Stop->request();
}

void SessionWorker::push_line(std::string Line) {
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        Pending.push_back(std::move(Line));
    }
    Wake();
}

void SessionWorker::relay_logs() {
    while (std::optional<std::string> Line = Logs->recv()) {
        push_line(std::move(*Line));
    }
}

void SessionWorker::run_task(const SessionTask &Task) {
    Outcome Finished;
    try {
        Task(Stop, Logs);
    } catch (const std::exception &Failure) {
        Finished.Failure = Failure.what();
    } catch (...) {
        Finished.Failure = "后台线程异常退出，未返回执行结果";
    }
    Logs->close();
    Relay.join(); // 队列里剩余的日志全部转交后才宣布完成
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        Result = std::move(Finished);
    }
    Wake();
}

std::vector<std::string> SessionWorker::take_logs() {
    std::lock_guard<std::mutex> Lock(Mutex);
    return std::exchange(Pending, {});
}

std::optional<SessionWorker::Outcome> SessionWorker::finished() {
    std::lock_guard<std::mutex> Lock(Mutex);
    return Result;
}

} // namespace scriptroom::gui
