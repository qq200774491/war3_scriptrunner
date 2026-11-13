#include "PillButton.h"

#include "Paint.h"

#include <cmath>

namespace scriptroom::gui {

namespace {

constexpr Rgb White = {255, 255, 255};

constexpr float DisabledAlpha = 0.4f;

struct ToneColors {
    Rgb Fill;
    float FillAlpha = 1.0f;
    Rgb Text;
};

ToneColors colors_of(const Palette &P, Tone Kind, const Surface::Pointer &State) {
    const bool Pressed = State.Pressed;
    const bool Hover = State.Hover;
    switch (Kind) {
    case Tone::Primary:
        return {Pressed ? P.AccentPressed : (Hover ? P.AccentHover : P.AccentSolid), 1.0f, White};
    case Tone::Danger:
        return {P.Danger, Pressed ? 0.20f : (Hover ? 0.12f : 0.0f), P.Danger};
    case Tone::Secondary:
        break;
    }
    return {P.Overlay, Pressed ? 0.14f : (Hover ? 0.08f : 0.0f), P.Text};
}

} // namespace

PillButton::PillButton(const Styler &Palettes, std::string Label, Tone Kind, Surface::ClickHandler OnClick)
    : State(std::make_shared<Look>(Look{std::move(Label), Kind})) {
    Area = Surface::create(
        [&Palettes, Shared = State](BuiRaster *Target, const Surface::Box &Box, const Surface &Self) {
            const Palette &P = Palettes.palette();
            const bool Enabled = Self.enabled();
            const Surface::Pointer Idle;
            const ToneColors Look = colors_of(P, Shared->Kind, Enabled ? Self.pointer() : Idle);
            const float Alpha = Enabled ? 1.0f : DisabledAlpha;
            const float Radius = Box.H * 0.5f;
            if (Look.FillAlpha > 0.0f) {
                paint::round_rect(Target, Box.X, Box.Y, Box.W, Box.H, Radius, Look.Fill, Look.FillAlpha * Alpha);
            }
            paint::text(Target, Box.X, Box.Y, Box.W, Box.H, Shared->Label, Look.Text, paint::Align::Center, Alpha);
        },
        std::move(OnClick));
}

void PillButton::set_enabled(bool On) const {
    Area->set_enabled(On);
}

void PillButton::set_label(std::string Label) const {
    if (State->Label != Label) {
        State->Label = std::move(Label);
        Area->invalidate();
    }
}

int PillButton::natural_width() const {
    const float Scale = BK_scale() > 0.0f ? BK_scale() : 1.0f;
    return static_cast<int>(std::ceil(paint::text_width(State->Label) / Scale)) + 36;
}

} // namespace scriptroom::gui
