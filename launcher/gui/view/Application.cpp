#include "Application.h"

#include "AppResources.h"
#include "Error.h"
#include "FileSystem.h"
#include "Geometry.h"
#include "Json.h"
#include "Png.h"
#include "Text.h"

#include <chrono>
#include <span>

namespace scriptroom::gui {

namespace {

/// 窗口回调与唤醒回调都是无数据的 C 函数指针，需要一个全局入口找到当前程序。只在界面线程读写。
Application *ActiveApplication = nullptr;

constexpr int MinimumWindowWidth = 900;
constexpr int MinimumWindowHeight = 580;

/// 等待宽限期时界面每隔多久推进一次状态机。其余情况由后台线程的唤醒驱动，不需要定时器。
constexpr int PollIntervalMs = 200;

AppDependencies make_dependencies(const LaunchOptions &Launch) {
    AppDependencies Deps;
    Deps.Root = find_root();
    Deps.Theme = Launch.Capture ? Launch.Capture->Theme : load_theme(Deps.Root);
    Deps.MakeTask = [Root = Deps.Root](const Config &Settings, SessionMode Mode) {
        return make_session_task(Root, Settings, Mode);
    };
    Deps.Games = make_system_game_backend();
    Deps.Wake = [] { BK_window_wake(); };
    return Deps;
}

/// 任务栏、Alt+Tab 和标题栏读的是窗口图标，不会自动取 EXE 内嵌的图标资源，建窗后要显式设置。
/// 两种尺寸都是系统标准图标尺寸，可以用共享句柄（LR_SHARED），由系统负责释放。
void apply_app_icon(HWND Window) {
    const HINSTANCE Module = GetModuleHandleW(nullptr);
    const auto load = [Module](int Width, int Height) {
        return reinterpret_cast<LPARAM>(
            LoadImageW(Module, MAKEINTRESOURCEW(IDI_SCRIPTROOM), IMAGE_ICON, Width, Height, LR_SHARED));
    };
    SendMessageW(Window, WM_SETICON, ICON_BIG, load(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON)));
    SendMessageW(Window, WM_SETICON, ICON_SMALL, load(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON)));
}

} // namespace

Application::Application(LaunchOptions Options)
    : Launch(std::move(Options)), Model(make_dependencies(Launch)), Tree(Node::create("frame")),
      Context(Tree.root()) {
    Tree.root().num("events", 1.0); // 根节点上报尺寸变化，用于响应式隐藏详情栏
    const BKHost Host = Router.host();
    BK_context_set_host(Context.get(), &Host);
    Router.set_on_failure([this](const std::string &Message) {
        Model.report(Message);
        if (View) {
            View->sync();
        }
    });

    if (Launch.Capture) {
        Model.preview(Launch.Capture->Start, Launch.Capture->Theme, std::string());
    } else {
        open_window();
    }
    build_view();
    ActiveApplication = this;
}

Application::~Application() {
    ActiveApplication = nullptr;
}

void Application::open_window() {
    BKWindow *Created = BK_window_new(Context.get(), "云脚本Test启动器", Launch.Width, Launch.Height);
    require(Created != nullptr, "无法创建窗口");
    Window = std::make_unique<WindowHandle>(Created);
    const auto Handle = static_cast<HWND>(BK_window_handle(Created));
    apply_app_icon(Handle);
    Chrome = std::make_unique<WindowChrome>(Created, Context.get(), geometry::TopBarHeight, geometry::RailWidth);
    Chrome->resize_client(Launch.Width, Launch.Height);

    BKWindowCallbacks Callbacks = {};
    Callbacks.data = this;
    Callbacks.on_timer = &Application::on_timer;
    Callbacks.on_close = &Application::on_close;
    BK_window_set_callbacks(Created, &Callbacks);
    BK_window_wm(Created, "minsize", MinimumWindowWidth, MinimumWindowHeight);
}

void Application::build_view() {
    View = std::make_unique<MainView>(Model, Router, Tree.root(), [this]() -> HWND {
        return Window ? static_cast<HWND>(BK_window_handle(Window->get())) : nullptr;
    });
    Router.on(Tree.root(), BK_EV_CONFIGURE, [this](double Width, double Height) {
        View->resize(static_cast<int>(Width), static_cast<int>(Height));
    });
    Router.set_after_event([this] { refresh(); });
    if (Chrome) {
        Chrome->round_corners(Tree.root());
        Chrome->set_outline(palette_of(Model.theme()).Outline);
    }
}

int Application::run() {
    return Launch.Capture ? run_capture(*Launch.Capture) : run_window();
}

void Application::refresh() {
    Model.tick(AppModel::Clock::now());
    View->sync();
    if (Chrome) {
        Chrome->set_outline(palette_of(Model.theme()).Outline);
    }
    arm_timer();
    if (Model.quit_requested()) {
        request_window_close();
    }
}

void Application::arm_timer() {
    if (!Window || TimerArmed || !Model.needs_timer()) {
        return;
    }
    TimerArmed = true;
    BK_window_timer(Window->get(), PollIntervalMs);
}

void Application::request_window_close() {
    if (CloseRequested || !Window) {
        return;
    }
    CloseRequested = true;
    // 走 WM_CLOSE 而不是直接销毁：此刻可能正处在 BUI 的输入处理里，同步销毁窗口并不安全。
    PostMessageW(static_cast<HWND>(BK_window_handle(Window->get())), WM_CLOSE, 0, 0);
}

void Application::on_wake() {
    try {
        if (ActiveApplication != nullptr) {
            ActiveApplication->refresh();
        }
    } catch (...) {
        // 异常不得穿过 C 回调；下一次事件会再次同步。
    }
}

void Application::on_timer(void *Self, BKWindow *, int) {
    try {
        auto &App = *static_cast<Application *>(Self);
        App.TimerArmed = false;
        App.refresh();
    } catch (...) {
    }
}

int Application::on_close(void *Self, BKWindow *) {
    try {
        auto &App = *static_cast<Application *>(Self);
        if (App.Model.request_window_close()) {
            return 1;
        }
        App.refresh(); // 弹出“云服务正在运行”的确认框
        return 0;
    } catch (...) {
        return 1; // 界面状态异常时宁可关闭窗口，也不要卡住退出
    }
}

int Application::run_window() {
    require(BK_window_supported() != 0, "当前系统不支持原生窗口");
    require(BK_window_wake_init(&Application::on_wake) != 0, "无法创建跨线程唤醒通道");
    BK_window_show(Window->get());
    if (Launch.AutoStart) {
        Model.start(SessionMode::Run);
    }
    refresh();
    BK_window_run();
    return 0;
}

int Application::run_capture(const CaptureRequest &Request) {
    if (Request.SampleStats) {
        Model.preview_stats(sample_stats_board());
    }
    if (Request.LogFile) {
        try {
            Model.append_log(read_utf8_file(*Request.LogFile));
            Model.report("历史日志预览");
        } catch (const std::exception &) {
            // 与 Rust 版一致：读不到日志文件就按空控制台截图。
        }
    }
    const int Width = Launch.Width;
    const int Height = Launch.Height;
    View->resize(Width, Height);
    View->sync();

    const RasterHandle Raster(Width, Height);
    // 第一遍布局并让自绘节点（折行文本）算出自己的高度，第二遍才是稳定的画面。
    BK_render(Context.get(), Raster.get(), Width, Height, 1);
    View->sync();
    BK_render(Context.get(), Raster.get(), Width, Height, 1);

    const std::size_t Count = static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height);
    write_png(Request.Output, Width, Height, std::span<const std::uint32_t>(BUI_raster_pixels(Raster.get()), Count));

    const Json Meta = {
        {"theme", theme_key(Request.Theme)},
        {"page", page_key(Request.Start)},
        {"scale", BK_scale()},
        {"image_pixels", {Width, Height}},
        {"logical_size", {Width, Height}},
    };
    fs::path MetaPath = Request.Output;
    atomic_write(MetaPath.replace_extension(".json"), dump_compact(Meta));
    return 0;
}

} // namespace scriptroom::gui
