#include "LogView.h"

#include "Paint.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace scriptroom::gui {

namespace {

/// 超长日志行只绘制开头，避免为每次重绘排版几千字节的文本。
constexpr std::size_t MaxDrawnBytes = 600;

constexpr int WheelStepRows = 3;

} // namespace

/// 一次布局得到的各项尺寸（物理像素）。
struct LogView::Metrics {
    int X = 0; ///< 节点左上角（窗口坐标）
    int Y = 0;
    int Width = 0;
    int Height = 0;
    int HeaderHeight = px(24);
    int RowHeight = px(27);
    int Pad = px(8);
    int NumberWidth = px(40);
    int SourceWidth = px(52);
    int BarWidth = px(6);

    int body_height() const { return std::max(0, Height - HeaderHeight); }
    int visible_rows() const { return std::max(1, body_height() / RowHeight); }
};

/// 滚动条滑块在行表主体内的位置与长度（物理像素）。
struct LogView::Thumb {
    float Offset = 0.0f;
    float Length = 0.0f;
    float Span = 1.0f; ///< 滑块可移动的总距离
};

LogView::LogView(SelectHandler Notify) : Handle(Node::create("custom")), OnSelect(std::move(Notify)) {
    BKCustomClass Class = {};
    Class.event = &LogView::on_event;
    Class.draw = &LogView::on_draw;
    Class.natural_w = px(240);
    Class.natural_h = px(160);
    BK_custom_set(Handle.raw(), &Class, this);
}

void LogView::set_palette(const Palette &NewColors) {
    Colors = NewColors;
    invalidate();
}

void LogView::invalidate() const {
    BK_node_invalidate(Handle.raw());
}

LogView::Metrics LogView::metrics() const {
    int Rect[4] = {};
    BK_node_rect(Handle.raw(), Rect);
    Metrics Layout;
    Layout.X = Rect[0];
    Layout.Y = Rect[1];
    Layout.Width = Rect[2];
    Layout.Height = Rect[3];
    return Layout;
}

LogView::Thumb LogView::thumb(const Metrics &Layout) const {
    const int Count = std::max(1, static_cast<int>(Rows.size()));
    const float Track = static_cast<float>(Layout.body_height());
    Thumb Result;
    Result.Length = std::max(static_cast<float>(px(28)),
                             Track * static_cast<float>(Layout.visible_rows()) / static_cast<float>(Count));
    Result.Span = std::max(1.0f, Track - Result.Length);
    Result.Offset = Result.Span * static_cast<float>(Top) / static_cast<float>(std::max(1, max_top()));
    return Result;
}

int LogView::max_top() const {
    return std::max(0, static_cast<int>(Rows.size()) - metrics().visible_rows());
}

void LogView::scroll_to(int First) {
    const int Clamped = std::clamp(First, 0, max_top());
    if (Clamped != Top) {
        Top = Clamped;
        invalidate();
    }
}

void LogView::scroll_by(int RowsDelta) {
    scroll_to(Top + RowsDelta);
    Stuck = Top >= max_top();
}

void LogView::show(const LogModel &Source, const LogFilter &Filter, const std::optional<std::string> &Selected,
                   bool Follow) {
    const bool ContentChanged = !HasApplied || Logs != &Source || AppliedRevision != Source.revision() ||
                                !(AppliedFilter == Filter);
    Logs = &Source;
    if (ContentChanged) {
        Rows = Source.visible(Filter);
        AppliedFilter = Filter;
        AppliedRevision = Source.revision();
        HasApplied = true;
        Hover = -1;
        invalidate();
    }
    if (Chosen != Selected || FollowEnabled != Follow) {
        Chosen = Selected;
        FollowEnabled = Follow;
        invalidate();
    }
}

int LogView::row_at(float Y) const {
    const Metrics Layout = metrics();
    if (Y < static_cast<float>(Layout.HeaderHeight)) {
        return -1;
    }
    const int Slot = Top + static_cast<int>((Y - static_cast<float>(Layout.HeaderHeight)) /
                                           static_cast<float>(Layout.RowHeight));
    return Slot < static_cast<int>(Rows.size()) ? Slot : -1;
}

bool LogView::in_scrollbar(float X) const {
    const Metrics Layout = metrics();
    return static_cast<int>(Rows.size()) > Layout.visible_rows() && X >= static_cast<float>(Layout.Width - px(14));
}

void LogView::pick(int Slot) {
    if (Slot < 0 || Slot >= static_cast<int>(Rows.size())) {
        return;
    }
    const std::string &Line = Logs->at(Rows[static_cast<std::size_t>(Slot)]).Text;
    Chosen = Line;
    invalidate();
    if (OnSelect) {
        OnSelect(Line);
    }
}

void LogView::select_neighbor(int Step) {
    if (Rows.empty()) {
        return;
    }
    int Current = -1;
    for (std::size_t Slot = 0; Chosen && Slot < Rows.size(); ++Slot) {
        if (Logs->at(Rows[Slot]).Text == *Chosen) {
            Current = static_cast<int>(Slot);
            break;
        }
    }
    const int Last = static_cast<int>(Rows.size()) - 1;
    const int Next = Current < 0 ? (Step > 0 ? 0 : Last) : std::clamp(Current + Step, 0, Last);
    pick(Next);
    const int Visible = metrics().visible_rows();
    if (Next < Top) {
        scroll_to(Next);
    } else if (Next >= Top + Visible) {
        scroll_to(Next - Visible + 1);
    }
    Stuck = Top >= max_top();
}

void LogView::begin_bar_drag(float Y) {
    const Metrics Layout = metrics();
    const Thumb Bar = thumb(Layout);
    const float ThumbTop = static_cast<float>(Layout.HeaderHeight) + Bar.Offset;
    if (Y < ThumbTop || Y > ThumbTop + Bar.Length) { // 点在轨道上：把滑块中心移到点击处
        const float Fraction = (Y - static_cast<float>(Layout.HeaderHeight) - Bar.Length * 0.5f) / Bar.Span;
        scroll_to(static_cast<int>(std::lround(std::clamp(Fraction, 0.0f, 1.0f) * static_cast<float>(max_top()))));
        Stuck = Top >= max_top();
    }
    DraggingBar = true;
    DragStartY = Y;
    DragStartTop = Top;
}

void LogView::drag_bar(float Y) {
    const float Moved = (Y - DragStartY) * static_cast<float>(max_top()) / thumb(metrics()).Span;
    scroll_to(DragStartTop + static_cast<int>(std::lround(Moved)));
    Stuck = Top >= max_top();
}

int LogView::handle_event(const BKCustomEvent &Event) {
    switch (Event.type) {
    case BK_CUSTOM_PRESS:
        if (Event.button != 1) {
            return 0;
        }
        if (in_scrollbar(Event.x)) {
            begin_bar_drag(Event.y);
        } else {
            pick(row_at(Event.y));
        }
        return 1;
    case BK_CUSTOM_RELEASE:
        DraggingBar = false;
        return 1;
    case BK_CUSTOM_MOTION: {
        if (DraggingBar) {
            drag_bar(Event.y);
            return 1;
        }
        const int Slot = row_at(Event.y);
        if (Slot != Hover) {
            Hover = Slot;
            invalidate();
        }
        return 0;
    }
    case BK_CUSTOM_LEAVE:
        if (Hover != -1) {
            Hover = -1;
            invalidate();
        }
        return 0;
    case BK_CUSTOM_WHEEL:
        scroll_by(-static_cast<int>(std::lround(Event.wheel_y * static_cast<float>(WheelStepRows))));
        return 1;
    case BK_CUSTOM_KEY:
        break;
    default:
        return 0;
    }

    const std::string Key = Event.keysym;
    const int Page = std::max(1, metrics().visible_rows() - 1);
    if (Key == "Up" || Key == "Down") {
        select_neighbor(Key == "Up" ? -1 : 1);
    } else if (Key == "Prior" || Key == "Next") {
        select_neighbor(Key == "Prior" ? -Page : Page);
    } else if (Key == "Home" || Key == "End") {
        select_neighbor(Key == "Home" ? -static_cast<int>(Rows.size()) : static_cast<int>(Rows.size()));
    } else if ((Event.mods & BK_CTRL) != 0 && (Key == "c" || Key == "C") && Chosen) {
        BK_clipboard_set(BK_node_context(Handle.raw()), Chosen->c_str());
    } else {
        return 0;
    }
    return 1;
}

int LogView::on_event(void *Self, BKNode *, const BKCustomEvent *Event) {
    try {
        return static_cast<LogView *>(Self)->handle_event(*Event);
    } catch (...) {
        return 0; // 异常不得穿过 C 回调
    }
}

void LogView::on_draw(void *Self, BKNode *, BuiRaster *Target, const int[4]) {
    try {
        static_cast<LogView *>(Self)->draw(Target);
    } catch (...) {
        // 绘制失败只丢这一帧，不能让异常穿过 C 回调。
    }
}

void LogView::draw(BuiRaster *Target) {
    const Metrics Layout = metrics();
    paint::fill(Target, static_cast<float>(Layout.X), static_cast<float>(Layout.Y), static_cast<float>(Layout.Width),
                static_cast<float>(Layout.Height), Colors.Canvas);
    draw_header(Target, Layout);

    const int Count = static_cast<int>(Rows.size());
    Top = std::clamp(Top, 0, std::max(0, Count - Layout.visible_rows()));
    if (FollowEnabled && Stuck) {
        Top = std::max(0, Count - Layout.visible_rows());
    }
    const int Shown = std::min(Layout.visible_rows() + 1, Count - Top);
    for (int Offset = 0; Offset < Shown; ++Offset) {
        draw_row(Target, Layout, Top + Offset);
    }
    draw_scrollbar(Target, Layout);
}

void LogView::draw_header(BuiRaster *Target, const Metrics &Layout) {
    const float X = static_cast<float>(Layout.X);
    const float Y = static_cast<float>(Layout.Y);
    const float H = static_cast<float>(Layout.HeaderHeight);
    const float NumberX = X + static_cast<float>(Layout.Pad);
    const float SourceX = NumberX + static_cast<float>(Layout.NumberWidth);
    const float ContentX = SourceX + static_cast<float>(Layout.SourceWidth);
    paint::text(Target, NumberX, Y, static_cast<float>(Layout.NumberWidth), H, "#", Colors.Muted);
    paint::text(Target, SourceX, Y, static_cast<float>(Layout.SourceWidth), H, "来源", Colors.Muted);
    paint::text(Target, ContentX, Y, static_cast<float>(Layout.Width) - (ContentX - X), H, "内容", Colors.Muted);
    paint::fill(Target, X, Y + H - 1.0f, static_cast<float>(Layout.Width), 1.0f, Colors.Divider);
}

void LogView::draw_row(BuiRaster *Target, const Metrics &Layout, int Slot) {
    const LogEntry &Entry = Logs->at(Rows[static_cast<std::size_t>(Slot)]);
    const float X = static_cast<float>(Layout.X);
    const float Y = static_cast<float>(Layout.Y + Layout.HeaderHeight + (Slot - Top) * Layout.RowHeight);
    const float H = static_cast<float>(Layout.RowHeight);
    const float W = static_cast<float>(Layout.Width);

    Rgb Background = Slot % 2 == 0 ? Colors.Canvas : Colors.Panel;
    if (Chosen && *Chosen == Entry.Text) {
        Background = Colors.Selected;
    } else if (Slot == Hover) {
        Background = Colors.Raised;
    }
    paint::fill(Target, X, Y, W, H, Background);

    const float NumberX = X + static_cast<float>(Layout.Pad);
    const float SourceX = NumberX + static_cast<float>(Layout.NumberWidth);
    const float ContentX = SourceX + static_cast<float>(Layout.SourceWidth);
    // 行号是保留窗口内的序号，所以用 Rows 里的原始下标而不是筛选后的位置。
    paint::text(Target, NumberX, Y, static_cast<float>(Layout.NumberWidth), H,
                std::format("{:03}", Rows[static_cast<std::size_t>(Slot)] + 1), Colors.Muted);
    const Rgb SourceColor = Entry.Source == LogSource::Error ? Colors.Danger : Colors.Muted;
    paint::text(Target, SourceX, Y, static_cast<float>(Layout.SourceWidth), H, source_label(Entry.Source), SourceColor);
    paint::text(Target, ContentX, Y, W - (ContentX - X) - static_cast<float>(px(10)), H,
                std::string(paint::utf8_prefix(Entry.Text, MaxDrawnBytes)), Colors.Text);
}

void LogView::draw_scrollbar(BuiRaster *Target, const Metrics &Layout) {
    if (static_cast<int>(Rows.size()) <= Layout.visible_rows()) {
        return;
    }
    const Thumb Bar = thumb(Layout);
    const float BarX = static_cast<float>(Layout.X + Layout.Width - Layout.BarWidth - px(2));
    paint::fill(Target, BarX, static_cast<float>(Layout.Y + Layout.HeaderHeight) + Bar.Offset,
                static_cast<float>(Layout.BarWidth), Bar.Length, Colors.Border);
}

} // namespace scriptroom::gui
