// “关闭全部魔兽”状态机：请求 -> 等待 -> 确认（已捕获目标）-> 后台结束。

#include "GuiFakes.h"
#include "TestHarness.h"

#include <atomic>

namespace {

using namespace scriptroom::gui;
using namespace gui_fakes;
using namespace testing;

struct Harness {
    std::shared_ptr<World> State = std::make_shared<World>();
    std::atomic<int> Wakes{0};
    GameCloser Closer;
    GameCloser::Clock::time_point Start = GameCloser::Clock::now();

    Harness() : Closer(std::make_unique<FakeBackend>(State), [this] { ++Wakes; }) {}

    std::vector<std::string> after(std::chrono::seconds Elapsed) { return Closer.poll(Start + Elapsed); }
};

bool contains(const std::vector<std::string> &Messages, std::string_view Needle) {
    return std::any_of(Messages.begin(), Messages.end(),
                       [&](const std::string &Message) { return Message.find(Needle) != std::string::npos; });
}

const Registrar CloseWithoutGames("gui.close.no_games", [] {
    Harness Test;
    const std::vector<std::string> Messages = Test.Closer.request(Test.Start);
    check(Messages == std::vector<std::string>({"没有运行中的魔兽"}));
    check(!Test.Closer.busy());
    check(!Test.Closer.needs_polling());
});

const Registrar CloseWaitsBeforeChecking("gui.close.waits_for_grace_period", [] {
    Harness Test;
    Test.State->Games = {game(11), game(22)};
    const std::vector<std::string> Messages = Test.Closer.request(Test.Start);
    check_equal(Messages.size(), std::size_t(2));
    check(contains(Messages, "11") && contains(Messages, "22"));
    check(Test.Closer.busy());
    check(Test.Closer.needs_polling());
    check(!Test.Closer.awaiting_confirmation());
    check_equal(Test.State->CloseRequested.size(), std::size_t(2));

    check(Test.after(std::chrono::seconds(1)).empty());
    check(Test.after(std::chrono::seconds(2)).empty());
    check_equal(Test.State->list_calls(), 1); // 宽限期内不重新枚举
    check(Test.Closer.busy());
});

const Registrar CloseCapturesBeforeConfirmation("gui.close.captures_targets_before_confirmation", [] {
    Harness Test;
    Test.State->Games = {game(11), game(22)};
    Test.Closer.request(Test.Start);

    const std::vector<std::string> Messages = Test.after(GameCloser::GracePeriod);
    check(contains(Messages, "仍有 2 个魔兽进程；可结束 2 个"));
    check(Test.Closer.awaiting_confirmation());
    check(Test.Closer.pending_pids() == std::vector<std::uint32_t>({11, 22}));
    // 句柄在确认之前就已取得，此时还没有结束任何进程。
    check(Test.State->Captured == std::vector<std::uint32_t>({11, 22}));
    check(Test.State->terminated().empty());
    check(!Test.Closer.needs_polling()); // 等用户确认，不需要定时器
    check(Test.after(std::chrono::seconds(30)).empty());
    check(Test.Closer.awaiting_confirmation());
});

const Registrar CloseTerminatesOnlyCapturedTargets("gui.close.terminate_uses_captured_targets", [] {
    Harness Test;
    Test.State->Games = {game(11), game(22)};
    Test.Closer.request(Test.Start);
    Test.after(GameCloser::GracePeriod);

    // 确认之前进程号被别的进程复用，也不应影响已捕获的目标，更不允许重新按进程号查询。
    Test.State->Games = {game(11), game(999)};
    const int ListCallsBefore = Test.State->list_calls();
    Test.Closer.terminate();
    check(Test.Closer.busy());
    check(!Test.Closer.awaiting_confirmation());

    std::vector<std::string> Messages;
    const bool Finished = spin_until(
        [&] {
            for (std::string &Message : Test.after(std::chrono::seconds(10))) {
                Messages.push_back(std::move(Message));
            }
        },
        [&] { return !Test.Closer.busy(); });
    check(Finished, "后台结束线程没有完成");
    check(Test.State->terminated() == std::vector<std::uint32_t>({11, 22}));
    check_equal(Test.State->list_calls(), ListCallsBefore);
    check_equal(Messages.size(), std::size_t(3));
    check_equal(Messages[0], std::string("已结束进程 11"));
    check_equal(Messages[1], std::string("已结束进程 22"));
    check(Messages[2].find("结束操作已完成") != std::string::npos);
    check(Test.Wakes.load() >= 3); // 每条结果和结束标记都唤醒界面线程
});

const Registrar ClosePartialCaptureFailure("gui.close.capture_failures_are_reported", [] {
    Harness Test;
    Test.State->Games = {game(11), game(22)};
    Test.State->DenyCapture = {22};
    Test.Closer.request(Test.Start);
    const std::vector<std::string> Messages = Test.after(GameCloser::GracePeriod);
    check(contains(Messages, "无权结束进程 22"));
    check(contains(Messages, "仍有 2 个魔兽进程；可结束 1 个"));
    check(Test.Closer.pending_pids() == std::vector<std::uint32_t>({11}));

    // 全部无权限：没有可确认的目标，回到空闲。
    Harness Denied;
    Denied.State->Games = {game(5)};
    Denied.State->DenyCapture = {5};
    Denied.Closer.request(Denied.Start);
    const std::vector<std::string> DeniedMessages = Denied.after(GameCloser::GracePeriod);
    check(contains(DeniedMessages, "可结束 0 个"));
    check(!Denied.Closer.busy());
});

const Registrar CloseAllExited("gui.close.games_exit_during_grace_period", [] {
    Harness Test;
    Test.State->Games = {game(11)};
    Test.Closer.request(Test.Start);
    Test.State->Games.clear();
    const std::vector<std::string> Messages = Test.after(GameCloser::GracePeriod);
    check(Messages == std::vector<std::string>({"全部魔兽已退出"}));
    check(!Test.Closer.busy());
});

const Registrar CloseCancelDropsTargets("gui.close.cancel_discards_targets", [] {
    Harness Test;
    Test.State->Games = {game(11), game(22)};
    Test.Closer.request(Test.Start);
    Test.after(GameCloser::GracePeriod);
    Test.Closer.cancel();
    check(!Test.Closer.busy());
    check(Test.State->terminated().empty());
    check_equal(Test.State->TargetsDestroyed, 2); // 句柄被释放
    check_throws([&] { Test.Closer.terminate(); }, "没有待确认");
});

const Registrar CloseReportsTerminationFailures("gui.close.terminate_failure_does_not_stop_others", [] {
    Harness Test;
    Test.State->Games = {game(11), game(22)};
    Test.State->FailTerminate = {11};
    Test.Closer.request(Test.Start);
    Test.after(GameCloser::GracePeriod);
    Test.Closer.terminate();

    std::vector<std::string> Messages;
    check(spin_until(
        [&] {
            for (std::string &Message : Test.after(std::chrono::seconds(10))) {
                Messages.push_back(std::move(Message));
            }
        },
        [&] { return !Test.Closer.busy(); }));
    check_equal(Messages.size(), std::size_t(3));
    check_equal(Messages[0], std::string("无法结束进程 11"));
    check_equal(Messages[1], std::string("已结束进程 22"));
});

const Registrar CloseEnumerationFailures("gui.close.enumeration_failures", [] {
    Harness Test;
    Test.State->ListFails = true;
    check_throws([&] { Test.Closer.request(Test.Start); }, "枚举进程失败");
    check(!Test.Closer.busy()); // 请求失败不改变状态

    Test.State->ListFails = false;
    Test.State->Games = {game(1)};
    Test.Closer.request(Test.Start);
    Test.State->ListFails = true;
    const std::vector<std::string> Messages = Test.after(GameCloser::GracePeriod);
    check(Messages == std::vector<std::string>({"枚举进程失败"}));
    check(!Test.Closer.busy());
});

} // namespace
