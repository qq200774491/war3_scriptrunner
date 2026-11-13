#pragma once

#include "BuiNode.h"

#include <functional>

namespace scriptroom::gui {

class Surface {
public:
    struct Pointer {
        bool Hover = false;
        bool Pressed = false;
        float X = 0.0f;
        float Y = 0.0f;
    };

    struct Box {
        float X = 0.0f;
        float Y = 0.0f;
        float W = 0.0f;
        float H = 0.0f;
    };

    using Painter = std::function<void(BuiRaster *Target, const Box &Area, const Surface &Self)>;
    using ClickHandler = std::function<void(const Pointer &State)>;

    /// 返回的对象归节点所有，节点释放时删除。
    static Surface *create(Painter Paint, ClickHandler OnClick = {});

    Surface(const Surface &) = delete;
    Surface &operator=(const Surface &) = delete;

    Node node() const { return Handle; }

    void fill_parent(Node Parent) const;

    void set_enabled(bool On);
    bool enabled() const { return Enabled; }

    const Pointer &pointer() const { return State; }
    void invalidate() const;

private:
    Surface(Painter Paint, ClickHandler OnClick);
    ~Surface() = default;

    static int on_event(void *Self, BKNode *Source, const BKCustomEvent *Event);
    static void on_draw(void *Self, BKNode *Source, BuiRaster *Target, const int Rect[4]);
    static void on_free(void *Self, BKNode *Source);

    int handle_event(const BKCustomEvent &Event);
    bool inside(float X, float Y) const;

    Node Handle;
    Painter Paint;
    ClickHandler OnClick;
    Pointer State;
    bool Enabled = true;
};

} // namespace scriptroom::gui
