#pragma once

#include "ActionBar.h"
#include "NavRow.h"
#include "ViewServices.h"

#include <array>
#include <memory>

namespace scriptroom::gui {

class RailBar {
public:
    RailBar(const ViewServices &Context, Node Parent);

    void sync();

private:
    const ViewServices &Services;
    std::array<std::unique_ptr<RailKey>, 5> Keys;
};

class NavigationPanel {
public:
    NavigationPanel(const ViewServices &Context, Node Parent);

    void sync();

private:
    const ViewServices &Services;
    std::unique_ptr<ActionBar> Actions;
    std::array<std::unique_ptr<NavRow>, 4> ScopeRows;
};

} // namespace scriptroom::gui
