#include "Styler.h"

namespace scriptroom::gui {

void Styler::paint(const Painted &Item) const {
    const Rgb Color = Current.*Item.Role;
    if (Item.IsText) {
        Item.Target.text_color(Color);
    } else if (Item.Alpha <= 0.0f) {
        Item.Target.no_background();
    } else {
        Item.Target.background(Color, Item.Alpha);
    }
}

void Styler::background(Node Target, ColorRole Role, float Alpha) {
    Items.push_back({Target, Role, Alpha, false});
    paint(Items.back());
}

void Styler::foreground(Node Target, ColorRole Role) {
    Items.push_back({Target, Role, 1.0f, true});
    paint(Items.back());
}

void Styler::watch(std::function<void(const Palette &)> Reapply) {
    Reapply(Current);
    Watchers.push_back(std::move(Reapply));
}

void Styler::apply(const Palette &Next) {
    Current = Next;
    for (const Painted &Item : Items) {
        paint(Item);
    }
    for (const auto &Reapply : Watchers) {
        Reapply(Current);
    }
}

} // namespace scriptroom::gui
