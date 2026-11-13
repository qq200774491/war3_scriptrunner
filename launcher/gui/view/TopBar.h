#pragma once

#include "ViewServices.h"

namespace scriptroom::gui {

class TopBar {
public:
    TopBar(const ViewServices &Context, Node Parent);

    void sync();

private:
    void build_brand(Node Bar);

    const ViewServices &Services;
    Node Search;
};

} // namespace scriptroom::gui
