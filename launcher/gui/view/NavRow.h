#pragma once

#include "Builder.h"

#include <functional>
#include <string>
#include <string_view>

namespace scriptroom::gui {

class NavRow {
public:
    struct Look {
        const char *Icon = nullptr;
        std::string Text;
        Rgb Tint;
    };

    NavRow(const Builder &Ui, Node Parent, const Pack &Layout, const Look &Appearance, std::function<void()> OnClick);
    NavRow(const NavRow &) = delete;
    NavRow &operator=(const NavRow &) = delete;

    void set_selected(bool On);
    void set_count(std::string_view Text);

private:
    void paint(BuiRaster *Target, const Surface::Box &Area, const Surface &Self) const;
    void refresh_labels() const;

    Styler &Colors;
    Surface *Area = nullptr;
    Node IconLabel;
    Node Title;
    Rgb Tint;
    std::string Count;
    bool Selected = false;
};

class RailKey {
public:
    RailKey(const Builder &Ui, Node Parent, const Pack &Layout, const char *Icon, std::string_view Tooltip,
            std::function<void()> OnClick);
    RailKey(const RailKey &) = delete;
    RailKey &operator=(const RailKey &) = delete;

    void set_selected(bool On);

private:
    void paint(BuiRaster *Target, const Surface::Box &Area, const Surface &Self) const;

    const Styler &Colors;
    Surface *Area = nullptr;
    bool Selected = false;
};

} // namespace scriptroom::gui
