#pragma once

#include "BuiNode.h"

#include <windows.h>

namespace scriptroom::gui {

class WindowChrome {
public:
    WindowChrome(BKWindow *Target, BKContext *Context, int CaptionHeight, int CaptionLeft);
    ~WindowChrome();
    WindowChrome(const WindowChrome &) = delete;
    WindowChrome &operator=(const WindowChrome &) = delete;

    void resize_client(int Width, int Height) const;

    /// 系统不画圆角时（Win10），在 Root 的四角补上自绘的圆角。要在其余界面建好之后调用。
    void round_corners(Node Root) const;

private:
    static LRESULT CALLBACK on_message(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam, UINT_PTR Id,
                                       DWORD_PTR Data);

    LRESULT hit_test(LPARAM ScreenPoint) const;
    void fit_maximized(RECT &Proposed) const;

    HWND Window;
    BKContext *Context;
    int CaptionHeight;
    int CaptionLeft;
    bool Attached = false;
    bool FadesCorners = false;
};

} // namespace scriptroom::gui
