#include "WindowChrome.h"

#include "RoundedCorners.h"

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

// Win11（构建号 22000 起）由 DWM 给窗口画圆角，Win10 要自己画。
bool system_rounds_corners() {
    using GetVersionFn = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);
    const auto Get = reinterpret_cast<GetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    RTL_OSVERSIONINFOW Info = {};
    Info.dwOSVersionInfoSize = sizeof(Info);
    return Get != nullptr && Get(&Info) == 0 && Info.dwBuildNumber >= 22000;
}

// Win10 的 DWM 只在开了背后模糊时才按客户区像素的 alpha 合成；模糊区域给空区域，就只透明不模糊。
// GPU 渲染的交换链没有 alpha，这时保持直角。
bool composite_with_alpha(HWND Window, BuiRaster *Raster) {
    if (Raster == nullptr || BUI_raster_is_gpu(Raster) != 0 || BUI_raster_gpu_pending(Raster) != 0) {
        return false;
    }
    const HRGN Empty = CreateRectRgn(0, 0, -1, -1);
    const DWM_BLURBEHIND Blur = {DWM_BB_ENABLE | DWM_BB_BLURREGION, TRUE, Empty, FALSE};
    const bool Enabled = SUCCEEDED(DwmEnableBlurBehindWindow(Window, &Blur));
    DeleteObject(Empty);
    if (Enabled) {
        BUI_raster_set_alpha(Raster, 1);
    }
    return Enabled;
}

int frame_thickness(HWND Window) {
    const UINT Dpi = GetDpiForWindow(Window);
    return GetSystemMetricsForDpi(SM_CXSIZEFRAME, Dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, Dpi);
}

} // namespace

WindowChrome::WindowChrome(BKWindow *Target, BKContext *Ui, int Caption, int CaptionStart)
    : Window(static_cast<HWND>(BK_window_handle(Target))), Context(Ui), CaptionHeight(Caption),
      CaptionLeft(CaptionStart) {
    if (Window == nullptr) {
        return;
    }
    Attached = SetWindowSubclass(Window, &WindowChrome::on_message, SubclassId, reinterpret_cast<DWORD_PTR>(this)) != FALSE;
    if (!Attached) {
        return;
    }
    if (system_rounds_corners()) {
        DwmSetWindowAttribute(Window, CornerPreference, &RoundCorners, sizeof(RoundCorners));
    } else {
        FadesCorners = composite_with_alpha(Window, BK_window_raster(Target));
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

void WindowChrome::round_corners(Node Root) const {
    if (FadesCorners) {
        // 只捕获窗口句柄：节点树比本对象活得久，窗口销毁后 IsZoomed 只会返回假。
        add_rounded_corners(Root, px(CornerRadius), [Target = Window] { return IsZoomed(Target) == 0; });
    }
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
