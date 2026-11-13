#pragma once

#include "Styler.h"
#include "Surface.h"

#include <functional>
#include <memory>
#include <string>

namespace scriptroom::gui {

enum class Tone {
    Primary,
    Secondary,
    Danger,
};

class PillButton {
public:
    PillButton() = default;
    PillButton(const Styler &Colors, std::string Label, Tone Kind, Surface::ClickHandler OnClick);

    Node node() const { return Area->node(); }

    void set_enabled(bool On) const;
    void set_label(std::string Label) const;

    int natural_width() const;

private:
    struct Look {
        std::string Label;
        Tone Kind = Tone::Secondary;
    };

    Surface *Area = nullptr;
    std::shared_ptr<Look> State;
};

} // namespace scriptroom::gui
