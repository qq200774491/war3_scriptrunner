#pragma once

#include "PillButton.h"
#include "ViewServices.h"

namespace scriptroom::gui {

class ActionBar {
public:
    ActionBar(const ViewServices &Context, Node Head, Node Body);

    void sync();

private:
    void paint_status(BuiRaster *Target, const Surface::Box &Area) const;

    const ViewServices &Services;
    PillButton StartButton;
    PillButton CheckButton;
    PillButton StopButton;
    PillButton CloseGamesButton;
    Surface *Status = nullptr;
};

} // namespace scriptroom::gui
