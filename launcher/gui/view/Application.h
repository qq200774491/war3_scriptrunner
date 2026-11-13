#pragma once

#include "AppModel.h"
#include "BuiHandles.h"
#include "CommandLine.h"
#include "EventRouter.h"
#include "MainView.h"
#include "WindowChrome.h"

#include <memory>

namespace scriptroom::gui {

/// 程序本体：持有状态模型、BUI 节点树 / 上下文 / 窗口，并驱动它们。
///
/// 窗口模式下进入 BUI 的消息循环；截图模式不创建窗口，渲染一帧后写出 PNG。
/// 所有 BUI 调用都在创建本对象的线程（界面线程）。后台线程只通过 BK_window_wake 唤醒界面线程。
class Application {
public:
    explicit Application(LaunchOptions Launch);
    ~Application();
    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;

    /// 运行到窗口关闭（或截图完成）；返回进程退出码。
    int run();

private:
    // 窗口回调与唤醒回调是 C 函数指针，在这里一次性转成成员调用，并拦住所有 C++ 异常。
    static void on_wake();
    static void on_timer(void *Self, BKWindow *Window, int Timer);
    static int on_close(void *Self, BKWindow *Window);

    void open_window();
    void build_view();
    int run_window();
    int run_capture(const CaptureRequest &Request);

    /// 事件、唤醒或定时器之后：推进模型、同步控件、必要时安排下一次定时器或请求关闭窗口。
    void refresh();
    void arm_timer();
    void request_window_close();

    LaunchOptions Launch;
    AppModel Model;
    EventRouter Router;
    NodeTree Tree;
    std::unique_ptr<MainView> View;
    ContextHandle Context;
    std::unique_ptr<WindowHandle> Window;
    std::unique_ptr<WindowChrome> Chrome; ///< 必须在 Window 之后声明：先于窗口析构
    bool TimerArmed = false;
    bool CloseRequested = false;
};

} // namespace scriptroom::gui
