#pragma once

#include "ViewServices.h"

namespace scriptroom::gui {

class TopBar {
public:
    TopBar(const ViewServices &Context, Node Parent);

    void sync();

    void set_inspector_visible(bool Shown);

private:
    void build_brand(Node Bar);

    const ViewServices &Services;
    Node Search;
    Node RightSegment;
    bool InspectorShown = true;
};

} // namespace scriptroom::gui
