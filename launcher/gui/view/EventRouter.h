#pragma once

#include "BuiNode.h"

#include <functional>
#include <map>
#include <string>
#include <utility>

namespace scriptroom::gui {

/// 把 BUI 的 C 回调（BKHost.emit）转成 C++ 回调。
///
/// 路由表只登记界面构建时创建的节点，节点随 NodeTree 一起存在，所以不需要注销。
/// C++ 异常不会穿过 C 回调：处理函数抛出的异常交给 on_failure。
class EventRouter {
public:
    /// a、b 的含义随事件种类而定（见 BUI_kit.h 的 BK_EV_*）。
    using Handler = std::function<void(double A, double B)>;

    /// 每个事件处理完后调用一次（界面在这里把模型状态同步到控件）。
    void set_after_event(std::function<void()> Hook) { AfterEvent = std::move(Hook); }
    void set_on_failure(std::function<void(const std::string &)> Sink) { Failure = std::move(Sink); }

    void on(Node Source, int Kind, Handler Body);
    /// 常用的无参形式。
    void on_click(Node Source, std::function<void()> Body);
    void on_change(Node Source, std::function<void()> Body);

    /// 只填 data 与 emit 的 BKHost，交给 BK_context_set_host（窗口宿主会补全其余服务）。
    BKHost host();

private:
    static void emit(void *Self, BKNode *Source, int Kind, void *Ref, double A, double B);
    void dispatch(BKNode *Source, int Kind, double A, double B);

    std::map<std::pair<BKNode *, int>, Handler> Handlers;
    std::function<void()> AfterEvent;
    std::function<void(const std::string &)> Failure;
};

} // namespace scriptroom::gui
