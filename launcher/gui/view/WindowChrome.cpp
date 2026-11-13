#include "WindowChrome.h"

#include "BuiNode.h"

#include <array>
#include <string_view>

#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>

namespace scriptroom::gui {

namespace {

constexpr UINT_PTR SubclassId = 0x5352;

constexpr int ResizeBorder = 6;
constexpr int CornerRadius = 8;
constexpr DWORD CornerPreference = 33; // DWMWA_WINDOW_CORNER_PREFERENCE，Win11 SDK 才有这个名字
constexpr int RoundCorners = 2;        // DWMWCP_ROUND

constexpr std::array<std::array<LRESULT, 3>, 3> EdgeHits = {{
    {HTTOPLEFT, HTTOP, HTTOPRIGHT},
    {HTLEFT, HTCLIENT, HTRIGHT},
    {HTBOTTOMLEFT, HTBOTTOM, HTBOTTOMRIGHT},
}};

std::size_t band(LONG Value, LONG Extent, LONG Border) {
    if (Value < Border) {
        return 0;
    }
    return Value >= Extent - Border ? 2 : 1;
}

// 根 frame 打开了 events，BK_hit_at 在空白处也会返回它。
bool is_control(BKNode *Hit) {
    return Hit != nullptr && std::string_view(BK_get_str(Hit, "type")) != "frame";
}

// Win11（构建号 22000 起）由 DWM 给窗口画圆角，Win10 需要自己用窗口区域裁出来。
bool system_rounds_corners() {
    using GetVersionFn = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);
    const auto Get = reinterpret_cast<GetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    RTL_OSVERSIONINFOW Info = {};
    Info.dwOSVersionInfoSize = sizeof(Info);
    return Get != nullptr && Get(&Info) == 0 && Info.dwBuildNumber >= 22000;
}

int frame_thickness(HWND Window) {
    const UINT Dpi = GetDpiForWindow(Window);
    return GetSystemMetricsForDpi(SM_CXSIZEFRAME, Dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, Dpi);
}

} // namespace

WindowChrome::WindowChrome(HWND Target, BKContext *Ui, int Caption, int CaptionStart)
    : Window(Target), Context(Ui), CaptionHeight(Caption), CaptionLeft(CaptionStart) {
    if (Window == nullptr) {
        return;
    }
    Attached = SetWindowSubclass(Window, &WindowChrome::on_message, SubclassId, reinterpret_cast<DWORD_PTR>(this)) != FALSE;
    if (!Attached) {
        return;
    }
    SystemCorners = system_rounds_corners();
    if (SystemCorners) {
        DwmSetWindowAttribute(Window, CornerPreference, &RoundCorners, sizeof(RoundCorners));
    }
    SetWindowPos(Window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

WindowChrome::~WindowChrome() {
    if (Attached) {
        RemoveWindowSubclass(Window, &WindowChrome::on_message, SubclassId);
    }
}

void WindowChrome::resize_client(int Width, int Height) const {
    if (!Attached) {
        return;
    }
    SetWindowPos(Window, nullptr, 0, 0, px(Width), px(Height), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// 最大化的窗口外框比显示器大一圈系统边框，客户区要收回这一圈。
void WindowChrome::fit_maximized(RECT &Proposed) const {
    const int Inset = frame_thickness(Window);
    Proposed.left += Inset;
    Proposed.top += Inset;
    Proposed.right -= Inset;
    Proposed.bottom -= Inset;
}

void WindowChrome::clip_corners() const {
    if (SystemCorners) {
        return;
    }
    if (IsZoomed(Window) != 0) {
        SetWindowRgn(Window, nullptr, TRUE);
        return;
    }
    RECT Bounds = {};
    GetWindowRect(Window, &Bounds);
    const int Diameter = px(CornerRadius) * 2;
    // 区域交给系统后由系统释放，这里不能再删除。
    SetWindowRgn(Window,
                 CreateRoundRectRgn(0, 0, Bounds.right - Bounds.left + 1, Bounds.bottom - Bounds.top + 1, Diameter, Diameter),
                 TRUE);
}

LRESULT WindowChrome::hit_test(LPARAM ScreenPoint) const {
    POINT Point = {GET_X_LPARAM(ScreenPoint), GET_Y_LPARAM(ScreenPoint)};
    ScreenToClient(Window, &Point);
    RECT Client = {};
    GetClientRect(Window, &Client);

    if (IsZoomed(Window) == 0) {
        const LONG Border = px(ResizeBorder);
        const LRESULT Edge = EdgeHits[band(Point.y, Client.bottom, Border)][band(Point.x, Client.right, Border)];
        if (Edge != HTCLIENT) {
            return Edge;
        }
    }
    const bool InCaption = Point.y < px(CaptionHeight) && Point.x >= px(CaptionLeft);
    if (InCaption && !is_control(BK_hit_at(Context, Point.x, Point.y))) {
        return HTCAPTION;
    }
    return HTCLIENT;
}

LRESULT CALLBACK WindowChrome::on_message(HWND Target, UINT Message, WPARAM WParam, LPARAM LParam, UINT_PTR,
                                          DWORD_PTR Data) {
    auto &Self = *reinterpret_cast<WindowChrome *>(Data);
    switch (Message) {
    case WM_NCCALCSIZE:
        if (WParam == TRUE) {
            if (IsZoomed(Target) != 0) {
                Self.fit_maximized(reinterpret_cast<NCCALCSIZE_PARAMS *>(LParam)->rgrc[0]);
            }
            return 0;
        }
        break;
    case WM_NCHITTEST:
        return Self.hit_test(LParam);
    case WM_NCACTIVATE:
        return DefWindowProcW(Target, Message, WParam, -1);
    case WM_SIZE:
        Self.clip_corners();
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(Target, &WindowChrome::on_message, SubclassId);
        Self.Attached = false;
        break;
    default:
        break;
    }
    return DefSubclassProc(Target, Message, WParam, LParam);
}

} // namespace scriptroom::gui
