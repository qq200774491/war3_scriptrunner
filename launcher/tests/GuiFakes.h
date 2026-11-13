#pragma once

// 图形界面测试共用的替身：不碰真实的魔兽进程。

#include "Error.h"
#include "GameCloser.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace gui_fakes {

/// 假后端与它派发的假目标共享的世界状态，测试从外部检查调用记录。
struct World {
    std::mutex Mutex;
    std::vector<scriptroom::GameProcess> Games;     ///< list() 返回的内容；测试可随时修改
    std::set<std::uint32_t> DenyCapture;            ///< capture() 会失败的进程
    std::set<std::uint32_t> FailTerminate;          ///< terminate() 会抛异常的进程
    bool ListFails = false;
    int ListCalls = 0;
    std::vector<std::uint32_t> CloseRequested;
    std::vector<std::uint32_t> Captured;
    std::vector<std::uint32_t> Terminated;          ///< 按结束顺序
    int TargetsDestroyed = 0;

    int list_calls() {
        std::lock_guard<std::mutex> Lock(Mutex);
        return ListCalls;
    }
    std::vector<std::uint32_t> terminated() {
        std::lock_guard<std::mutex> Lock(Mutex);
        return Terminated;
    }
};

class FakeTarget final : public scriptroom::gui::CloseTarget {
public:
    FakeTarget(std::shared_ptr<World> Shared, std::uint32_t Id) : State(std::move(Shared)), Pid(Id) {}
    ~FakeTarget() override {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        ++State->TargetsDestroyed;
    }

    std::uint32_t pid() const override { return Pid; }

    std::string terminate() override {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->FailTerminate.count(Pid) != 0) {
            throw scriptroom::Error("无法结束进程 " + std::to_string(Pid));
        }
        State->Terminated.push_back(Pid);
        return "已结束进程 " + std::to_string(Pid);
    }

private:
    std::shared_ptr<World> State;
    std::uint32_t Pid;
};

class FakeBackend final : public scriptroom::gui::GameBackend {
public:
    explicit FakeBackend(std::shared_ptr<World> Shared) : State(std::move(Shared)) {}

    std::vector<scriptroom::GameProcess> list() override {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        ++State->ListCalls;
        if (State->ListFails) {
            throw scriptroom::Error("枚举进程失败");
        }
        return State->Games;
    }

    std::string request_close(const scriptroom::GameProcess &Game) override {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        State->CloseRequested.push_back(Game.Pid);
        return "已请求关闭进程 " + std::to_string(Game.Pid);
    }

    std::unique_ptr<scriptroom::gui::CloseTarget> capture(const scriptroom::GameProcess &Game) override {
        {
            std::lock_guard<std::mutex> Lock(State->Mutex);
            if (State->DenyCapture.count(Game.Pid) != 0) {
                throw scriptroom::Error("无权结束进程 " + std::to_string(Game.Pid));
            }
            State->Captured.push_back(Game.Pid);
        }
        return std::make_unique<FakeTarget>(State, Game.Pid);
    }

private:
    std::shared_ptr<World> State;
};

inline scriptroom::GameProcess game(std::uint32_t Pid) {
    scriptroom::GameProcess Process;
    Process.Pid = Pid;
    Process.Name = "war3.exe";
    return Process;
}

/// 反复执行 Step 直到 Done 为真或超时；返回 Done 最终的值。
template <typename Step, typename Done> bool spin_until(Step &&Advance, Done &&IsDone, int TimeoutMs = 5000) {
    const auto Deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TimeoutMs);
    while (!IsDone()) {
        if (std::chrono::steady_clock::now() > Deadline) {
            return false;
        }
        Advance();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

} // namespace gui_fakes
