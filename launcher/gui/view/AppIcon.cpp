#include "AppIcon.h"

#include "AppResources.h"

#include <utility>

#include <windows.h>

namespace scriptroom::gui {

namespace {

struct IconHandle {
    HICON Raw = nullptr;
    ~IconHandle() {
        if (Raw != nullptr) {
            DestroyIcon(Raw);
        }
    }
};

struct BitmapHandle {
    HBITMAP Raw = nullptr;
    ~BitmapHandle() {
        if (Raw != nullptr) {
            DeleteObject(Raw);
        }
    }
};

struct ScreenDc {
    HDC Raw = GetDC(nullptr);
    ~ScreenDc() { ReleaseDC(nullptr, Raw); }
};

} // namespace

std::vector<std::uint8_t> app_icon_rgba(int Size) {
    if (Size <= 0) {
        return {};
    }
    IconHandle Icon;
    Icon.Raw = static_cast<HICON>(
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_SCRIPTROOM), IMAGE_ICON, Size, Size, 0));
    ICONINFO Info = {};
    if (Icon.Raw == nullptr || GetIconInfo(Icon.Raw, &Info) == 0) {
        return {};
    }
    BitmapHandle Color{Info.hbmColor};
    BitmapHandle Mask{Info.hbmMask};
    if (Color.Raw == nullptr) {
        return {};
    }

    BITMAPINFO Format = {};
    Format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Format.bmiHeader.biWidth = Size;
    Format.bmiHeader.biHeight = -Size;
    Format.bmiHeader.biPlanes = 1;
    Format.bmiHeader.biBitCount = 32;
    Format.bmiHeader.biCompression = BI_RGB;
    std::vector<std::uint8_t> Pixels(static_cast<std::size_t>(Size) * static_cast<std::size_t>(Size) * 4U);
    const ScreenDc Dc;
    if (GetDIBits(Dc.Raw, Color.Raw, 0, static_cast<UINT>(Size), Pixels.data(), &Format, DIB_RGB_COLORS) == 0) {
        return {};
    }
    for (std::size_t Offset = 0; Offset < Pixels.size(); Offset += 4) {
        std::swap(Pixels[Offset], Pixels[Offset + 2]);
    }
    return Pixels;
}

} // namespace scriptroom::gui
