#include "AppModel.h"

#include "Error.h"
#include "Text.h"

#include <format>

namespace scriptroom::gui {

AppModel::AppModel(AppDependencies Deps)
    : Root(std::move(Deps.Root)), ConfigPath(default_config(Root)), Theme(Deps.Theme),
      MakeTask(std::move(Deps.MakeTask)), Wake(Deps.Wake), Closer(std::move(Deps.Games), Deps.Wake) {
    try {
        Draft = ConfigDraft::from_config(Config::load(ConfigPath));
        Notice = "就绪";
    } catch (const std::exception &Failure) {
        Notice = std::format("配置未载入：{}", Failure.what());
    }
}

std::optional<Config> AppModel::resolved_config() const {
    try {
        return Draft.to_config(ConfigPath);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

void AppModel::show_scope(LogScope Scope) {
    Current = Page::Console;
    Filter.Scope = Scope;
}

void AppModel::select_log(std::string Line) {
    SelectedLog = std::move(Line);
    InspectorOpen = true;
}

void AppModel::clear_logs() {
    Logs.clear();
    SelectedLog.reset();
}

void AppModel::append_log(std::string_view Text) {
    Logs.append(Text);
}

void AppModel::set_theme(ThemeKind Kind) {
    Theme = Kind;
    try {
        save_theme(Root, Kind);
    } catch (const std::exception &Failure) {
        Notice = Failure.what();
    }
}

void AppModel::preview(Page Start, ThemeKind Kind, const std::string &Message) {
    Current = Start;
    Theme = Kind;
    if (!Message.empty()) {
        Notice = Message;
    }
}

void AppModel::log_notice(std::string Message) {
    Logs.append(Message);
    Notice = std::move(Message);
}

void AppModel::start(SessionMode Mode) {
    if (Worker) {
        return;
    }
    try {
        const Config Settings = Draft.to_config(ConfigPath);
        Worker = std::make_unique<SessionWorker>(MakeTask(Settings, Mode), Wake);
        Notice = Mode == SessionMode::Check ? "检查配置中" : "启动中";
        Current = Page::Console;
    } catch (const std::exception &Failure) {
        Notice = Failure.what();
        Current = Page::Settings;
    }
}

void AppModel::stop() {
    if (Worker) {
        Worker->stop();
        Notice = "正在停止云服务…";
    }
}

void AppModel::save_config() {
    try {
        Draft.to_config(ConfigPath).save(ConfigPath);
        Notice = std::format("已保存 {}", path_to_utf8(ConfigPath));
    } catch (const std::exception &Failure) {
        Notice = Failure.what();
    }
}

void AppModel::load_config(const fs::path &Path) {
    try {
        Draft = ConfigDraft::from_config(Config::load(Path));
        ++DraftRevision;
        ConfigPath = Path;
        Notice = "已载入配置";
    } catch (const std::exception &Failure) {
        Notice = Failure.what();
    }
}

void AppModel::save_config_as(const fs::path &Path) {
    try {
        Draft.to_config(ConfigPath).save(Path);
        ConfigPath = Path;
        Notice = "已保存配置";
    } catch (const std::exception &Failure) {
        Notice = Failure.what();
    }
}

void AppModel::close_games() {
    try {
        for (std::string &Message : Closer.request(Clock::now())) {
            log_notice(std::move(Message));
        }
        if (Closer.busy()) {
            Notice = "已请求正常关闭全部魔兽，等待退出…";
        }
    } catch (const std::exception &Failure) {
        Notice = Failure.what();
    }
}

void AppModel::confirm_force_close() {
    try {
        Closer.terminate();
        Notice = "正在后台结束魔兽，结果见控制台…";
    } catch (const std::exception &Failure) {
        Notice = Failure.what();
    }
}

void AppModel::cancel_force_close() {
    Closer.cancel();
}

bool AppModel::request_window_close() {
    if (!Worker) {
        return true;
    }
    ConfirmExit = true;
    return false;
}

void AppModel::keep_running() {
    ConfirmExit = false;
}

void AppModel::stop_and_exit() {
    ConfirmExit = false;
    if (!Worker) {
        QuitRequested = true;
        return;
    }
    ExitAfterStop = true;
    Worker->stop();
}

void AppModel::absorb_worker() {
    if (!Worker) {
        return;
    }
    // 先读完成状态再取日志：完成状态可见时，所有日志都已转交。
    const std::optional<SessionWorker::Outcome> Outcome = Worker->finished();
    for (const std::string &Line : Worker->take_logs()) {
        Logs.append(Line);
    }
    if (!Outcome) {
        return;
    }
    Worker.reset();
    log_notice(Outcome->Failure ? std::format("故障：{}", *Outcome->Failure) : std::string("已结束"));
    QuitRequested = QuitRequested || ExitAfterStop;
}

void AppModel::tick(Clock::time_point Now) {
    absorb_worker();
    for (std::string &Message : Closer.poll(Now)) {
        log_notice(std::move(Message));
    }
}

} // namespace scriptroom::gui
