#pragma once

#include "BuiNode.h"
#include "Surface.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <windows.h>

namespace scriptroom::gui {

class WindowChrome {
public:
    WindowChrome(BKWindow *Target, BKContext *Context, int CaptionHeight, int CaptionLeft);
    ~WindowChrome();
    WindowChrome(const WindowChrome &) = delete;
    WindowChrome &operator=(const WindowChrome &) = delete;

    void resize_client(int Width, int Height) const;

    /// 系统不画圆角时（Win10），在 Root 的四角和四边补上自绘的圆角与描边。要在其余界面建好之后调用。
    void round_corners(Node Root);

    /// 窗口外沿 1px 描边的颜色，跟着主题走：Win11 交给 DWM 的边框颜色，Win10 由自绘节点画。只在变化时生效。
    void set_outline(Rgb Color);

private:
    static LRESULT CALLBACK on_message(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam, UINT_PTR Id,
                                       DWORD_PTR Data);

    LRESULT hit_test(LPARAM ScreenPoint) const;
    void show_frame(bool Shown) const;
    void fit_maximized(RECT &Proposed) const;

    HWND Window;
    BKContext *Context;
    int CaptionHeight;
    int CaptionLeft;
    bool Attached = false;
    bool SystemCorners = false;
    bool FadesCorners = false;
    std::shared_ptr<std::uint32_t> Outline = std::make_shared<std::uint32_t>(0);
    std::optional<Rgb> AppliedOutline;
    std::vector<Surface *> FrameNodes;
};

} // namespace scriptroom::gui
