#include "EventRouter.h"

namespace scriptroom::gui {

void EventRouter::on(Node Source, int Kind, Handler Body) {
    Handlers[{Source.raw(), Kind}] = std::move(Body);
}

void EventRouter::on_click(Node Source, std::function<void()> Body) {
    on(Source, BK_EV_CLICK, [Body = std::move(Body)](double, double) { Body(); });
}

void EventRouter::on_change(Node Source, std::function<void()> Body) {
    on(Source, BK_EV_CHANGE, [Body = std::move(Body)](double, double) { Body(); });
}

BKHost EventRouter::host() {
    BKHost Host = {};
    Host.data = this;
    Host.emit = &EventRouter::emit;
    return Host;
}

void EventRouter::emit(void *Self, BKNode *Source, int Kind, void *, double A, double B) {
    static_cast<EventRouter *>(Self)->dispatch(Source, Kind, A, B);
}

void EventRouter::dispatch(BKNode *Source, int Kind, double A, double B) {
    const auto Found = Handlers.find({Source, Kind});
    if (Found == Handlers.end()) {
        return;
    }
    invoke([&Body = Found->second, A, B] { Body(A, B); });
}

void EventRouter::invoke(const std::function<void()> &Body) {
    try {
        Body();
        if (AfterEvent) {
            AfterEvent();
        }
    } catch (const std::exception &Reason) {
        if (Failure) {
            Failure(Reason.what());
        }
    } catch (...) {
        if (Failure) {
            Failure("界面事件处理出现未知错误");
        }
    }
}

} // namespace scriptroom::gui
