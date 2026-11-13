// 后台任务与界面状态模型：用假任务和假后端驱动，不启动任何游戏或服务。

#include "AppModel.h"
#include "FileSystem.h"
#include "GuiFakes.h"
#include "TestHarness.h"
#include "Text.h"

#include <atomic>

namespace {

using namespace scriptroom;
using namespace scriptroom::gui;
using namespace gui_fakes;
using namespace testing;

SessionTask quick_task() {
    return [](StopHandle, std::shared_ptr<LogChannel> Logs) {
        Logs->send("first");
        Logs->send("second\nthird");
    };
}

SessionTask failing_task() {
    return [](StopHandle, std::shared_ptr<LogChannel> Logs) {
        Logs->send("before failure");
        throw Error("boom");
    };
}

/// 一直运行到收到停止请求，模拟正在服务的房间。
SessionTask serving_task() {
    return [](StopHandle Stop, std::shared_ptr<LogChannel> Logs) {
        Logs->send("serving");
        while (!Stop->wait_for(std::chrono::milliseconds(5))) {
        }
        Logs->send("stopped");
    };
}

const Registrar WorkerDeliversLogsThenResult("gui.worker.logs_precede_completion", [] {
    std::atomic<int> Wakes{0};
    SessionWorker Worker(quick_task(), [&] { ++Wakes; });
    std::vector<std::string> Lines;
    std::optional<SessionWorker::Outcome> Outcome;
    check(spin_until(
        [&] {
            // 与界面线程相同的取数顺序：先看是否完成，再取日志，这样不会丢掉最后几行。
            Outcome = Worker.finished();
            for (std::string &Line : Worker.take_logs()) {
                Lines.push_back(std::move(Line));
            }
        },
        [&] { return Outcome.has_value(); }));
    check(!Outcome->Failure.has_value());
    check(Lines == std::vector<std::string>({"first", "second\nthird"}));
    check(Wakes.load() >= 3); // 每行日志和完成各唤醒一次（可合并，但至少有完成那次）
});

const Registrar WorkerReportsFailure("gui.worker.failure_message", [] {
    SessionWorker Worker(failing_task(), [] {});
    std::optional<SessionWorker::Outcome> Outcome;
    check(spin_until([&] { Outcome = Worker.finished(); }, [&] { return Outcome.has_value(); }));
    check(Outcome->Failure.has_value());
    check_equal(*Outcome->Failure, std::string("boom"));
    check(Worker.take_logs() == std::vector<std::string>({"before failure"}));
});

const Registrar WorkerStopsOnRequest("gui.worker.stop_request_ends_task", [] {
    SessionWorker Worker(serving_task(), [] {});
    std::vector<std::string> Lines;
    check(spin_until([&] { for (auto &Line : Worker.take_logs()) Lines.push_back(std::move(Line)); },
                     [&] { return !Lines.empty(); }));
    check(!Worker.finished().has_value()); // 还在服务
    Worker.stop();
    std::optional<SessionWorker::Outcome> Outcome;
    check(spin_until([&] { Outcome = Worker.finished(); }, [&] { return Outcome.has_value(); }));
    check(!Outcome->Failure.has_value());
    for (auto &Line : Worker.take_logs()) {
        Lines.push_back(std::move(Line));
    }
    check_equal(Lines.back(), std::string("stopped"));
});

/// 带一份有效配置的模型环境；Tasks 决定“启动”实际执行什么。
struct ModelFixture {
    TempDirectory Temp;
    std::shared_ptr<World> Games = std::make_shared<World>();
    std::atomic<int> Wakes{0};
    SessionTask Task = quick_task();
    std::optional<SessionMode> LastMode;
    std::unique_ptr<AppModel> Model;

    explicit ModelFixture(bool WithConfig = true) {
        if (WithConfig) {
            Config Settings;
            Settings.Map = Temp.path() / L"map.w3x";
            Settings.LocalEntry = Temp.path() / L"main.lua";
            Settings.CloudScripts = Temp.path() / L"cloud";
            Settings.Archives = Temp.path() / L"archives";
            Settings.Editor = Temp.path() / L"editor";
            Settings.SourceGame = Temp.path() / L"source";
            Settings.Game = Temp.path() / L"game";
            Settings.save(default_config(Temp.path()));
        }
        AppDependencies Deps;
        Deps.Root = Temp.path();
        Deps.MakeTask = [this](const Config &, SessionMode Mode) {
            LastMode = Mode;
            return Task;
        };
        Deps.Games = std::make_unique<FakeBackend>(Games);
        Deps.Wake = [this] { ++Wakes; };
        Model = std::make_unique<AppModel>(std::move(Deps));
    }

    void tick() { Model->tick(AppModel::Clock::now()); }

    bool wait_until_idle() {
        return spin_until([&] { tick(); }, [&] { return !Model->running(); });
    }
};

const Registrar ModelLoadsConfigOrReportsMissing("gui.model.initial_config", [] {
    ModelFixture Loaded;
    check_equal(Loaded.Model->notice(), std::string("就绪"));
    check(!Loaded.Model->draft().path(PathField::Map).empty());
    check_equal(Loaded.Model->draft().Seconds, std::string("3600"));
    check(Loaded.Model->page() == Page::Console);

    ModelFixture Missing(false);
    check(Missing.Model->notice().starts_with("配置未载入："));
    check(Missing.Model->draft().path(PathField::Map).empty());
    // 草稿为空时启动：提示原因并切到配置页，不会创建后台任务。
    Missing.Model->start(SessionMode::Run);
    check(!Missing.Model->running());
    check(Missing.Model->page() == Page::Settings);
    check(Missing.Model->notice().find("请补全") != std::string::npos);
    check(!Missing.LastMode.has_value());
});

const Registrar ModelRunsSessionToCompletion("gui.model.run_session_lifecycle", [] {
    ModelFixture Test;
    Test.Model->show_page(Page::Archives);
    Test.Task = serving_task();
    Test.Model->start(SessionMode::Run);
    check(Test.Model->running());
    check(Test.Model->page() == Page::Console); // 启动后回到控制台
    check_equal(Test.Model->notice(), std::string("启动中"));
    check(Test.LastMode == SessionMode::Run);

    Test.Model->stop();
    check_equal(Test.Model->notice(), std::string("正在停止云服务…"));
    check(Test.wait_until_idle());
    check_equal(Test.Model->notice(), std::string("已结束"));
    const LogModel &Logs = Test.Model->logs();
    check_equal(Logs.at(Logs.size() - 1).Text, std::string("已结束"));
    check_equal(Logs.at(0).Text, std::string("serving"));
    check_equal(Logs.at(Logs.size() - 2).Text, std::string("stopped"));
    check(Test.Wakes.load() > 0);
});

const Registrar ModelReportsFailureAndCheckMode("gui.model.failure_and_check_mode", [] {
    ModelFixture Test;
    Test.Task = failing_task();
    Test.Model->start(SessionMode::Check);
    check_equal(Test.Model->notice(), std::string("检查配置中"));
    check(Test.LastMode == SessionMode::Check);
    check(Test.wait_until_idle());
    check_equal(Test.Model->notice(), std::string("故障：boom"));
    check_equal(Test.Model->logs().at(Test.Model->logs().size() - 1).Text, std::string("故障：boom"));
    // 故障行会被归类为错误，出现在“错误与异常”范围里。
    check_equal(Test.Model->logs().count(LogScope::Errors), std::size_t(1));

    // 同一时间只有一个任务：运行中再次启动被忽略。
    Test.Task = serving_task();
    Test.Model->start(SessionMode::Run);
    Test.Model->start(SessionMode::Run);
    check(Test.Model->running());
    Test.Model->stop();
    check(Test.wait_until_idle());
});

const Registrar ModelClosingWindowWhileRunning("gui.model.window_close_flow", [] {
    ModelFixture Idle;
    check(Idle.Model->request_window_close()); // 没有服务在运行：直接关闭

    ModelFixture Test;
    Test.Task = serving_task();
    Test.Model->start(SessionMode::Run);
    check(!Test.Model->request_window_close());
    check(Test.Model->exit_confirmation_open());
    Test.Model->keep_running();
    check(!Test.Model->exit_confirmation_open());
    check(Test.Model->running());

    check(!Test.Model->request_window_close());
    Test.Model->stop_and_exit();
    check(!Test.Model->exit_confirmation_open());
    check(!Test.Model->quit_requested()); // 要等后台线程真正结束
    check(Test.wait_until_idle());
    check(Test.Model->quit_requested());

    ModelFixture Finished;
    Finished.Model->stop_and_exit(); // 服务已经结束时直接退出
    check(Finished.Model->quit_requested());
});

const Registrar ModelLogSelectionAndClearing("gui.model.log_selection", [] {
    ModelFixture Test;
    Test.Model->set_inspector_open(false);
    Test.Model->append_log("alpha\nbeta");
    Test.Model->select_log("beta");
    check(Test.Model->inspector_open()); // 点击日志行会展开详情栏
    check_equal(*Test.Model->selected_log(), std::string("beta"));

    Test.Model->show_scope(LogScope::Errors);
    check(Test.Model->filter().Scope == LogScope::Errors);
    check(Test.Model->page() == Page::Console);

    Test.Model->clear_logs();
    check(Test.Model->logs().empty());
    check(!Test.Model->selected_log().has_value());
});

const Registrar ModelConfigFiles("gui.model.save_load_config", [] {
    ModelFixture Test;
    Test.Model->draft().Seconds = "7200";
    Test.Model->save_config();
    check(Test.Model->notice().starts_with("已保存 "));
    check_equal(Config::load(Test.Model->config_path()).Seconds, std::uint64_t(7200));

    // 另存为：写到新文件并改用它。
    const fs::path Other = Test.Temp.path() / L"其他配置.json";
    Test.Model->draft().Seconds = "1800";
    Test.Model->save_config_as(Other);
    check_equal(Test.Model->notice(), std::string("已保存配置"));
    check_equal(Test.Model->config_path(), Other);
    check_equal(Config::load(Other).Seconds, std::uint64_t(1800));

    // 载入：整体替换草稿并让视图知道。
    const std::uint64_t Revision = Test.Model->draft_revision();
    Test.Model->load_config(default_config(Test.Temp.path()));
    check_equal(Test.Model->notice(), std::string("已载入配置"));
    check_equal(Test.Model->draft().Seconds, std::string("7200"));
    check(Test.Model->draft_revision() != Revision);

    // 载入失败：草稿与路径保持不变。
    const ConfigDraft Before = Test.Model->draft();
    Test.Model->load_config(Test.Temp.path() / L"不存在.json");
    check(Test.Model->draft() == Before);
    check(Test.Model->notice().find("不存在.json") != std::string::npos);

    // 草稿无效时保存失败，并说明原因。
    Test.Model->draft().Timeout = "abc";
    Test.Model->save_config();
    check(Test.Model->notice().find("启动超时") != std::string::npos);
});

const Registrar ModelThemePersists("gui.model.theme_is_saved", [] {
    ModelFixture Test;
    Test.Model->set_theme(ThemeKind::Light);
    check(Test.Model->theme() == ThemeKind::Light);
    check(load_theme(Test.Temp.path()) == ThemeKind::Light);
});

bool has_line(const LogModel &Logs, std::string_view Text) {
    for (std::size_t Index = 0; Index < Logs.size(); ++Index) {
        if (Logs.at(Index).Text == Text) {
            return true;
        }
    }
    return false;
}

const Registrar ModelCloseGamesFlow("gui.model.close_games_notices", [] {
    ModelFixture Test;
    Test.Model->close_games();
    check_equal(Test.Model->notice(), std::string("没有运行中的魔兽"));
    check(!Test.Model->closing_games());

    Test.Games->Games = {game(77)};
    Test.Model->close_games();
    check(Test.Model->closing_games());
    check_equal(Test.Model->notice(), std::string("已请求正常关闭全部魔兽，等待退出…"));
    check(Test.Model->needs_timer());
    // 请求结果同时写入控制台。
    check(has_line(Test.Model->logs(), "已请求关闭进程 77"));

    Test.Model->tick(AppModel::Clock::now() + std::chrono::seconds(4)); // 宽限期过后进入确认阶段
    check(Test.Model->awaiting_force_close());
    check(Test.Model->force_close_pids() == std::vector<std::uint32_t>({77}));
    Test.Model->cancel_force_close();
    check(!Test.Model->closing_games());
});

} // namespace
