#pragma once

#include "ViewServices.h"

namespace scriptroom::gui {

class AboutPage {
public:
    AboutPage(const ViewServices &Context, Node Parent);

    Node root() const { return Body; }

private:
    void add_row(Node Parent, std::string_view Title, std::string_view Value) const;

    const ViewServices &Services;
    Node Body;
};

} // namespace scriptroom::gui
