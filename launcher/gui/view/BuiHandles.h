#pragma once

#include "BuiNode.h"

#include <utility>

namespace scriptroom::gui {

/// BKContext 的所有者。释放顺序必须是：窗口 -> 上下文 -> 节点树。
class ContextHandle {
public:
    ContextHandle() = default;
    explicit ContextHandle(Node Root) : Raw(BK_context_new(Root.raw())) {}
    ~ContextHandle() { BK_context_free(Raw); }
    ContextHandle(const ContextHandle &) = delete;
    ContextHandle &operator=(const ContextHandle &) = delete;

    BKContext *get() const { return Raw; }

private:
    BKContext *Raw = nullptr;
};

/// BKWindow 的所有者；窗口不存在（截图模式）时为空。
class WindowHandle {
public:
    WindowHandle() = default;
    explicit WindowHandle(BKWindow *Created) : Raw(Created) {}
    ~WindowHandle() { BK_window_free(Raw); }
    WindowHandle(const WindowHandle &) = delete;
    WindowHandle &operator=(const WindowHandle &) = delete;

    BKWindow *get() const { return Raw; }
    explicit operator bool() const { return Raw != nullptr; }

private:
    BKWindow *Raw = nullptr;
};

/// BuiRaster 的所有者（截图用）。
class RasterHandle {
public:
    RasterHandle(int Width, int Height) : Raw(BUI_raster_create(Width, Height)) {}
    ~RasterHandle() { BUI_raster_free(Raw); }
    RasterHandle(const RasterHandle &) = delete;
    RasterHandle &operator=(const RasterHandle &) = delete;

    BuiRaster *get() const { return Raw; }

private:
    BuiRaster *Raw = nullptr;
};

} // namespace scriptroom::gui
