#include "GameCloser.h"

#include "Error.h"

#include <format>

namespace scriptroom::gui {

namespace {

/// 真实后端的强制结束句柄：包装 ForceTarget。
class SystemCloseTarget final : public CloseTarget {
public:
    explicit SystemCloseTarget(scriptroom::ForceTarget Captured) : Target(std::move(Captured)) {}

    std::uint32_t pid() const override { return Target.pid(); }
    std::string terminate() override { return Target.terminate(); }

private:
    scriptroom::ForceTarget Target;
};

class SystemGameBackend final : public GameBackend {
public:
    std::vector<scriptroom::GameProcess> list() override { return scriptroom::list_games(); }

    std::string request_close(const scriptroom::GameProcess &Game) override {
        try {
            return scriptroom::request_close(Game);
        } catch (const std::exception &Failure) {
            return Failure.what();
        }
    }

    std::unique_ptr<CloseTarget> capture(const scriptroom::GameProcess &Game) override {
        return std::make_unique<SystemCloseTarget>(scriptroom::ForceTarget::capture(Game));
    }
};

} // namespace

std::unique_ptr<GameBackend> make_system_game_backend() {
    return std::make_unique<SystemGameBackend>();
}

GameCloser::GameCloser(std::unique_ptr<GameBackend> System, std::function<void()> Notify)
    : Backend(std::move(System)), Wake(std::move(Notify)) {}

GameCloser::~GameCloser() {
    if (auto *Active = std::get_if<Running>(&Phase)) {
        Active->Worker.join();
    }
}

bool GameCloser::busy() const {
    return !std::holds_alternative<Idle>(Phase);
}

bool GameCloser::awaiting_confirmation() const {
    return std::holds_alternative<Confirm>(Phase);
}

bool GameCloser::needs_polling() const {
    return std::holds_alternative<Waiting>(Phase) || std::holds_alternative<Running>(Phase);
}

std::vector<std::uint32_t> GameCloser::pending_pids() const {
    std::vector<std::uint32_t> Pids;
    if (const auto *Pending = std::get_if<Confirm>(&Phase)) {
        for (const auto &Target : Pending->Targets) {
            Pids.push_back(Target->pid());
        }
    }
    return Pids;
}

std::vector<std::string> GameCloser::request(Clock::time_point Now) {
    const std::vector<scriptroom::GameProcess> Games = Backend->list();
    if (Games.empty()) {
        return {"没有运行中的魔兽"};
    }
    std::vector<std::string> Messages;
    for (const scriptroom::GameProcess &Game : Games) {
        Messages.push_back(Backend->request_close(Game));
    }
    Phase = Waiting{Now};
    return Messages;
}

std::vector<std::string> GameCloser::poll_waiting() {
    Phase = Idle{};
    std::vector<scriptroom::GameProcess> Games;
    try {
        Games = Backend->list();
    } catch (const std::exception &Failure) {
        return {Failure.what()};
    }
    std::vector<std::string> Messages;
    Confirm Pending;
    for (const scriptroom::GameProcess &Game : Games) {
        try {
            Pending.Targets.push_back(Backend->capture(Game));
        } catch (const std::exception &Failure) {
            Messages.push_back(Failure.what());
        }
    }
    if (Games.empty()) {
        Messages.push_back("全部魔兽已退出");
    } else {
        Messages.push_back(std::format("仍有 {} 个魔兽进程；可结束 {} 个，权限失败详情见控制台", Games.size(),
                                       Pending.Targets.size()));
    }
    if (!Pending.Targets.empty()) {
        Phase = std::move(Pending);
    }
    return Messages;
}

std::vector<std::string> GameCloser::poll_running(Running &Active) {
    std::vector<std::string> Messages;
    while (true) {
        // 先看是否已关闭再取值：结束标记总在关闭之前入队，不会把正常结束误判为中断。
        const bool Closed = Active.Updates->is_closed();
        std::optional<Update> Next = Active.Updates->try_recv();
        if (!Next) {
            if (!Closed) {
                break;
            }
            Messages.push_back("关闭任务意外中断，请检查逐个进程结果");
        } else if (!Next->Finished) {
            Messages.push_back(std::move(Next->Message));
            continue;
        } else {
            Messages.push_back("结束操作已完成，逐个进程结果见控制台");
        }
        Active.Worker.join();
        Phase = Idle{}; // Active 随之销毁，之后不再使用
        break;
    }
    return Messages;
}

std::vector<std::string> GameCloser::poll(Clock::time_point Now) {
    if (auto *Pending = std::get_if<Waiting>(&Phase)) {
        return Now - Pending->Since >= GracePeriod ? poll_waiting() : std::vector<std::string>();
    }
    if (auto *Active = std::get_if<Running>(&Phase)) {
        return poll_running(*Active);
    }
    return {};
}

void GameCloser::cancel() {
    Phase = Idle{};
}

void GameCloser::terminate_all(std::vector<std::unique_ptr<CloseTarget>> Targets, UpdateChannel &Updates,
                               const std::function<void()> &Wake) {
    const auto report = [&](Update Next) {
        Updates.send(std::move(Next));
        Wake();
    };
    for (const auto &Target : Targets) {
        std::string Message;
        try {
            Message = Target->terminate();
        } catch (const std::exception &Failure) {
            Message = Failure.what();
        }
        report(Update{std::move(Message), false});
    }
    report(Update{std::string(), true});
    Updates.close();
}

void GameCloser::terminate() {
    auto *Pending = std::get_if<Confirm>(&Phase);
    require(Pending != nullptr, "没有待确认的关闭目标");
    std::vector<std::unique_ptr<CloseTarget>> Targets = std::move(Pending->Targets);
    Phase = Idle{};

    Running Next;
    Next.Updates = std::make_shared<UpdateChannel>(Targets.size() + 2);
    Next.Worker = std::thread([Targets = std::move(Targets), Updates = Next.Updates, Wake = Wake]() mutable {
        terminate_all(std::move(Targets), *Updates, Wake);
    });
    Phase = std::move(Next);
}

} // namespace scriptroom::gui
