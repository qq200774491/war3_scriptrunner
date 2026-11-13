#pragma once

#include "Builder.h"

#include <functional>

#include <windows.h>

namespace scriptroom::gui {

void add_window_controls(const Builder &Ui, Node Parent, const Pack &Layout, std::function<HWND()> Window);

} // namespace scriptroom::gui
