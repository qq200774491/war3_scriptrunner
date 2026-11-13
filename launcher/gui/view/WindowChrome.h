#pragma once

#include "BUI_kit.h"

#include <windows.h>

namespace scriptroom::gui {

class WindowChrome {
public:
    WindowChrome(HWND Window, BKContext *Context, int CaptionHeight, int CaptionLeft);
    ~WindowChrome();
    WindowChrome(const WindowChrome &) = delete;
    WindowChrome &operator=(const WindowChrome &) = delete;

    void resize_client(int Width, int Height) const;

private:
    static LRESULT CALLBACK on_message(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam, UINT_PTR Id,
                                       DWORD_PTR Data);

    LRESULT hit_test(LPARAM ScreenPoint) const;
    void fit_maximized(RECT &Proposed) const;
    void clip_corners() const;

    HWND Window;
    BKContext *Context;
    int CaptionHeight;
    int CaptionLeft;
    bool Attached = false;
    bool SystemCorners = false;
};

} // namespace scriptroom::gui
