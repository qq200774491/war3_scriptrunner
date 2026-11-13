#pragma once

#include "BuiNode.h"
#include "LogModel.h"
#include "Theme.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace scriptroom::gui {

/// 控制台日志表：BUI 的 custom 节点，自己绘制行号 / 来源 / 内容三列。
///
/// 只绘制可见的行，5000 行也不需要为每行创建节点。
/// 与 Rust 版一致：选中按文本相等判断，跟随在滚离底部后暂停、回到底部后恢复。
/// 节点由 BUI 树持有，本对象由页面持有，二者生命周期一致；只在界面线程使用。
class LogView {
public:
    using SelectHandler = std::function<void(const std::string &Line)>;

    explicit LogView(SelectHandler Notify);
    LogView(const LogView &) = delete;
    LogView &operator=(const LogView &) = delete;

    /// 尚未加入父节点的节点；由调用方 add。
    Node node() const { return Handle; }

    void set_palette(const Palette &Colors);

    /// 把模型状态同步到视图；没有变化时什么也不做。Logs 只在本次调用与随后的绘制中被读取，
    /// 调用方必须保证它在下一次 show 之前一直存在。
    void show(const LogModel &Logs, const LogFilter &Filter, const std::optional<std::string> &Selected, bool Follow);

private:
    struct Metrics;
    struct Thumb;

    static int on_event(void *Self, BKNode *Source, const BKCustomEvent *Event);
    static void on_draw(void *Self, BKNode *Source, BuiRaster *Target, const int Rect[4]);

    int handle_event(const BKCustomEvent &Event);
    void draw(BuiRaster *Target);
    void draw_row(BuiRaster *Target, const Metrics &Layout, int Slot);
    void draw_header(BuiRaster *Target, const Metrics &Layout);
    void draw_scrollbar(BuiRaster *Target, const Metrics &Layout);
    Rgb source_color(LogSource Source) const;

    Metrics metrics() const;
    Thumb thumb(const Metrics &Layout) const;
    int max_top() const;
    int row_at(float Y) const;
    bool in_scrollbar(float X) const;
    void scroll_to(int First);
    void scroll_by(int Rows);
    void pick(int Slot);
    void select_neighbor(int Step);
    void begin_bar_drag(float Y);
    void drag_bar(float Y);
    void invalidate() const;

    Node Handle;
    SelectHandler OnSelect;
    Palette Colors;

    const LogModel *Logs = nullptr;
    LogFilter AppliedFilter;
    std::uint64_t AppliedRevision = 0;
    bool HasApplied = false;
    std::vector<std::size_t> Rows; ///< 通过筛选的日志下标
    std::optional<std::string> Chosen;
    bool FollowEnabled = true;
    bool Stuck = true; ///< 视图贴在底部；用户滚离后为 false
    int Top = 0;       ///< 第一条可见行在 Rows 中的位置
    int Hover = -1;    ///< 悬停行在 Rows 中的位置
    bool DraggingBar = false;
    float DragStartY = 0.0f;
    int DragStartTop = 0;
};

} // namespace scriptroom::gui
