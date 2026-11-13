#include "Surface.h"

namespace scriptroom::gui {

Surface *Surface::create(Painter Paint, ClickHandler OnClick) {
    return new Surface(std::move(Paint), std::move(OnClick));
}

Surface::Surface(Painter PaintBody, ClickHandler Click)
    : Handle(Node::create("custom")), Paint(std::move(PaintBody)), OnClick(std::move(Click)) {
    BKCustomClass Class = {};
    Class.event = &Surface::on_event;
    Class.draw = &Surface::on_draw;
    Class.free = &Surface::on_free;
    Class.natural_w = px(8);
    Class.natural_h = px(8);
    BK_custom_set(Handle.raw(), &Class, this);
}

void Surface::fill_parent(Node Parent) const {
    BK_node_add(Parent.raw(), Handle.raw(), -1);
    BK_geom_set_manager(Handle.raw(), BK_GEOM_PLACE);
    BK_geom_set(Handle.raw(), "relwidth", 1.0);
    BK_geom_set(Handle.raw(), "relheight", 1.0);
}

void Surface::set_enabled(bool On) {
    if (Enabled != On) {
        Enabled = On;
        State.Pressed = false;
        invalidate();
    }
}

void Surface::invalidate() const {
    BK_node_invalidate(Handle.raw());
}

bool Surface::inside(float X, float Y) const {
    int Rect[4] = {};
    BK_node_rect(Handle.raw(), Rect);
    return X >= 0.0f && Y >= 0.0f && X < static_cast<float>(Rect[2]) && Y < static_cast<float>(Rect[3]);
}

int Surface::handle_event(const BKCustomEvent &Event) {
    if (!OnClick) {
        return 0;
    }
    switch (Event.type) {
    case BK_CUSTOM_MOTION:
        State.X = Event.x;
        State.Y = Event.y;
        State.Hover = inside(Event.x, Event.y);
        invalidate();
        return 0;
    case BK_CUSTOM_LEAVE:
        State.Hover = false;
        State.Pressed = false;
        invalidate();
        return 0;
    case BK_CUSTOM_PRESS:
        if (Event.button != 1 || !Enabled) {
            return 0;
        }
        State.Pressed = true;
        State.X = Event.x;
        State.Y = Event.y;
        invalidate();
        return 1;
    case BK_CUSTOM_RELEASE: {
        if (Event.button != 1) {
            return 0;
        }
        const bool Clicked = State.Pressed && Enabled && inside(Event.x, Event.y);
        State.Pressed = false;
        State.X = Event.x;
        State.Y = Event.y;
        State.Hover = inside(Event.x, Event.y);
        invalidate();
        if (Clicked) {
            OnClick(State);
        }
        return 1;
    }
    default:
        return 0;
    }
}

int Surface::on_event(void *Self, BKNode *, const BKCustomEvent *Event) {
    try {
        return static_cast<Surface *>(Self)->handle_event(*Event);
    } catch (...) {
        return 0;
    }
}

void Surface::on_draw(void *Self, BKNode *, BuiRaster *Target, const int Rect[4]) {
    try {
        const auto &Area = *static_cast<Surface *>(Self);
        if (Area.Paint) {
            const Box Bounds = {static_cast<float>(Rect[0]), static_cast<float>(Rect[1]), static_cast<float>(Rect[2]),
                                static_cast<float>(Rect[3])};
            Area.Paint(Target, Bounds, Area);
        }
    } catch (...) {
    }
}

void Surface::on_free(void *Self, BKNode *) {
    delete static_cast<Surface *>(Self);
}

} // namespace scriptroom::gui
