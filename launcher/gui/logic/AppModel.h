#pragma once

#include "ConfigDraft.h"
#include "GameCloser.h"
#include "LogModel.h"
#include "Page.h"
#include "SessionWorker.h"
#include "Theme.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace scriptroom::gui {

namespace fs = std::filesystem;

/// 由配置草稿生成后台任务；测试可替换为不碰真实游戏的实现。
using SessionTaskFactory = std::function<SessionTask(const Config &, SessionMode)>;

/// AppModel 运行所需的外部依赖。
struct AppDependencies {
    fs::path Root;
    ThemeKind Theme = ThemeKind::Blue;
    SessionTaskFactory MakeTask;
    std::unique_ptr<GameBackend> Games;
    /// 后台线程用它唤醒界面线程；可从任意线程调用。
    std::function<void()> Wake;
};

/// 界面状态与全部操作，不依赖任何界面库。只在界面线程使用。
///
/// 视图在每次事件、唤醒和定时器之后调用 tick，再读取这里的状态刷新控件。
class AppModel {
public:
    using Clock = GameCloser::Clock;

    explicit AppModel(AppDependencies Deps);

    // ---- 只读状态 ----
    const fs::path &root() const { return Root; }
    const fs::path &config_path() const { return ConfigPath; }
    Page page() const { return Current; }
    ThemeKind theme() const { return Theme; }
    const std::string &notice() const { return Notice; }
    const LogModel &logs() const { return Logs; }
    const LogFilter &filter() const { return Filter; }
    bool follow() const { return Follow; }
    bool inspector_open() const { return InspectorOpen; }
    const std::optional<std::string> &selected_log() const { return SelectedLog; }
    bool running() const { return Worker != nullptr; }
    bool closing_games() const { return Closer.busy(); }
    bool awaiting_force_close() const { return Closer.awaiting_confirmation(); }
    std::vector<std::uint32_t> force_close_pids() const { return Closer.pending_pids(); }
    bool exit_confirmation_open() const { return ConfirmExit; }
    /// 后台线程结束后需要真正关闭窗口。
    bool quit_requested() const { return QuitRequested; }

    /// 配置草稿；视图直接编辑它。load/save 之类整体替换草稿的操作会令 draft_revision 加一。
    ConfigDraft &draft() { return Draft; }
    const ConfigDraft &draft() const { return Draft; }
    std::uint64_t draft_revision() const { return DraftRevision; }

    /// 草稿当前能否解析成配置（检查器展示运行路径用）；不能时为空。
    std::optional<Config> resolved_config() const;

    /// 需要周期性 tick（等待宽限期时）。其余情况由 Wake 触发 tick。
    bool needs_timer() const { return Closer.needs_polling(); }

    /// 界面事件处理中出现的错误：显示在状态栏。
    void report(std::string Message) { Notice = std::move(Message); }

    // ---- 导航与显示 ----
    void show_page(Page Target) { Current = Target; }
    void show_scope(LogScope Scope);
    void set_query(std::string Query) { Filter.Query = std::move(Query); }
    void set_follow(bool On) { Follow = On; }
    void set_inspector_open(bool On) { InspectorOpen = On; }
    void select_log(std::string Line);
    void clear_logs();
    /// 追加日志文本（按行拆分）；用于预览历史日志。
    void append_log(std::string_view Text);
    void set_theme(ThemeKind Kind);
    /// 截图模式使用：直接设定页面、主题和提示，不写盘。
    void preview(Page Start, ThemeKind Kind, const std::string &Message);

    // ---- 服务 ----
    /// 启动或检查。草稿无效时切换到配置页并在提示里说明原因。
    void start(SessionMode Mode);
    void stop();

    // ---- 配置文件 ----
    void save_config();
    void load_config(const fs::path &Path);
    void save_config_as(const fs::path &Path);

    // ---- 关闭全部魔兽 ----
    void close_games();
    void confirm_force_close();
    void cancel_force_close();

    // ---- 关闭窗口 ----
    /// 窗口收到关闭请求：服务在运行时先询问并返回 false，否则返回 true 表示可以关闭。
    bool request_window_close();
    void keep_running();
    void stop_and_exit();

    /// 收取后台日志与完成状态，推进关闭魔兽状态机。
    void tick(Clock::time_point Now);

private:
    void log_notice(std::string Message);
    void absorb_worker();

    fs::path Root;
    fs::path ConfigPath;
    ConfigDraft Draft;
    std::uint64_t DraftRevision = 0;
    ThemeKind Theme;
    Page Current = Page::Console;
    std::string Notice;
    LogModel Logs;
    LogFilter Filter;
    bool Follow = true;
    bool InspectorOpen = true;
    std::optional<std::string> SelectedLog;
    SessionTaskFactory MakeTask;
    std::function<void()> Wake;
    std::unique_ptr<SessionWorker> Worker;
    GameCloser Closer;
    bool ConfirmExit = false;
    bool ExitAfterStop = false;
    bool QuitRequested = false;
};

} // namespace scriptroom::gui
