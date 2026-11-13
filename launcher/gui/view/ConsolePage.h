#pragma once

#include "LogView.h"
#include "ViewServices.h"

namespace scriptroom::gui {

class ConsolePage {
public:
    ConsolePage(const ViewServices &Context, Node Parent);

    Node root() const { return Body; }
    void sync();

private:
    const ViewServices &Services;
    Node Body;
    Node FollowCheck;
    PillButton CopyLine;
    Node EmptyState;
    LogView Log;
};

} // namespace scriptroom::gui
