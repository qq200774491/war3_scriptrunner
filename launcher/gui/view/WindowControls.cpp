#include "WindowControls.h"

#include "Paint.h"

#include <array>
#include <optional>

namespace scriptroom::gui {

namespace {

enum class WindowKey { Minimize, Maximize, Close };

struct KeyColors {
    WindowKey Key;
    Rgb Normal;
    Rgb Pressed;
};

constexpr std::array<KeyColors, 3> Keys = {{
    {WindowKey::Minimize, {0x0D, 0xA5, 0x37}, {0x0B, 0x8E, 0x2F}},
    {WindowKey::Maximize, {0xC8, 0x9C, 0x00}, {0xAB, 0x85, 0x00}},
    {WindowKey::Close, {0xCA, 0x3E, 0x37}, {0xAD, 0x35, 0x30}},
}};

constexpr int DotRadius = 7;
constexpr int DotPitch = 28;
constexpr int RingMargin = 4;
constexpr int ControlsWidth = RingMargin * 2 + DotRadius * 2 * 3 + (DotPitch - DotRadius * 2) * 2;

constexpr Rgb Black = {0, 0, 0};

float dot_center_x(const Surface::Box &Area, std::size_t Index) {
    return Area.X + static_cast<float>(px(RingMargin + DotRadius + DotPitch * static_cast<int>(Index)));
}

std::optional<std::size_t> key_at(float X) {
    for (std::size_t Index = 0; Index < Keys.size(); ++Index) {
        const float Center = static_cast<float>(px(RingMargin + DotRadius + DotPitch * static_cast<int>(Index)));
        if (X >= Center - static_cast<float>(px(DotRadius + RingMargin)) &&
            X < Center + static_cast<float>(px(DotRadius + RingMargin))) {
            return Index;
        }
    }
    return std::nullopt;
}

void draw_glyph(BuiRaster *Target, WindowKey Key, float Cx, float Cy, bool Zoomed) {
    const float U = static_cast<float>(px(1));
    const float Stroke = 1.4f * U;
    switch (Key) {
    case WindowKey::Minimize:
        paint::fill(Target, Cx - 3.0f * U, Cy - 1.0f * U, 6.0f * U, 2.0f * U, Black, 0.5f);
        return;
    case WindowKey::Maximize: {
        const auto square = [&](float Left, float Top, float Size) {
            paint::line(Target, Left, Top, Left + Size, Top, Stroke, Black, 0.5f);
            paint::line(Target, Left, Top + Size, Left + Size, Top + Size, Stroke, Black, 0.5f);
            paint::line(Target, Left, Top, Left, Top + Size, Stroke, Black, 0.5f);
            paint::line(Target, Left + Size, Top, Left + Size, Top + Size, Stroke, Black, 0.5f);
        };
        if (Zoomed) {
            square(Cx - 4.0f * U, Cy - 2.0f * U, 5.0f * U);
            paint::line(Target, Cx - 2.0f * U, Cy - 4.0f * U, Cx + 3.0f * U, Cy - 4.0f * U, Stroke, Black, 0.5f);
            paint::line(Target, Cx + 3.0f * U, Cy - 4.0f * U, Cx + 3.0f * U, Cy + 1.0f * U, Stroke, Black, 0.5f);
        } else {
            square(Cx - 3.0f * U, Cy - 3.0f * U, 6.0f * U);
        }
        return;
    }
    case WindowKey::Close:
        paint::line(Target, Cx - 3.0f * U, Cy - 3.0f * U, Cx + 3.0f * U, Cy + 3.0f * U, Stroke, Black, 0.5f);
        paint::line(Target, Cx - 3.0f * U, Cy + 3.0f * U, Cx + 3.0f * U, Cy - 3.0f * U, Stroke, Black, 0.5f);
        return;
    }
}

void run_key(WindowKey Key, HWND Window) {
    if (Window == nullptr) {
        return;
    }
    switch (Key) {
    case WindowKey::Minimize:
        ShowWindow(Window, SW_MINIMIZE);
        return;
    case WindowKey::Maximize:
        ShowWindow(Window, IsZoomed(Window) ? SW_RESTORE : SW_MAXIMIZE);
        return;
    case WindowKey::Close:
        PostMessageW(Window, WM_CLOSE, 0, 0);
        return;
    }
}

} // namespace

void add_window_controls(const Builder &Ui, Node Parent, const Pack &Layout, std::function<HWND()> Window) {
    Pack Shape = Layout;
    Shape.Width = ControlsWidth;
    const auto paint_keys = [Window](BuiRaster *Target, const Surface::Box &Area, const Surface &Self) {
        const Surface::Pointer &State = Self.pointer();
        const std::optional<std::size_t> Hovered = State.Hover ? key_at(State.X) : std::nullopt;
        const HWND Handle = Window ? Window() : nullptr;
        const bool Zoomed = Handle != nullptr && IsZoomed(Handle) != 0;
        const float Cy = Area.Y + Area.H * 0.5f;
        for (std::size_t Index = 0; Index < Keys.size(); ++Index) {
            const float Cx = dot_center_x(Area, Index);
            const bool Active = Hovered == Index;
            const bool Down = Active && State.Pressed;
            if (Active) {
                const int Ring = Down ? 2 : RingMargin;
                paint::disc(Target, Cx, Cy, static_cast<float>(px(DotRadius + Ring)), Keys[Index].Normal, 0.33f);
            }
            paint::disc(Target, Cx, Cy, static_cast<float>(px(DotRadius)), Down ? Keys[Index].Pressed : Keys[Index].Normal);
            if (Hovered) {
                draw_glyph(Target, Keys[Index].Key, Cx, Cy, Zoomed);
            }
        }
    };
    const auto click_key = [Window](const Surface::Pointer &State) {
        if (const std::optional<std::size_t> Index = key_at(State.X)) {
            run_key(Keys[*Index].Key, Window ? Window() : nullptr);
        }
    };
    Ui.surface(Parent, Shape, paint_keys, click_key);
}

} // namespace scriptroom::gui
