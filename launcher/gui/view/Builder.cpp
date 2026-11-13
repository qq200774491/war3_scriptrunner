#include "Builder.h"

namespace scriptroom::gui {

Node Builder::frame(Node Parent, const Pack &Layout, std::optional<ColorRole> Background, int Padding) const {
    const Node Created = Parent.add(Node::create("frame"), Layout);
    Created.num("padding", px(Padding));
    if (Background) {
        Style.background(Created, *Background);
    }
    return Created;
}

Node Builder::card(Node Parent, const Pack &Layout, int Padding, ColorRole Fill) const {
    const Node Outline = frame(Parent, Layout, &Palette::Border, 1);
    return frame(Outline, {.Fill = Stretch::Both, .Expand = true}, Fill, Padding);
}

Node Builder::divider(Node Parent, const Pack &Layout, bool Vertical) const {
    Pack Shape = Layout;
    (Vertical ? Shape.Width : Shape.Height) = 1;
    const Node Line = Parent.add(Node::create("frame"), Shape);
    Style.background(Line, &Palette::Divider);
    return Line;
}

Node Builder::label(Node Parent, const Pack &Layout, std::string_view Text, const TextStyle &Look) const {
    const Node Created = Parent.add(Node::create("label"), Layout);
    Created.str("text", Text);
    Created.str("text_align", Look.Align);
    Created.num("font_scale", Look.Scale);
    Created.num("bold", Look.Bold ? 1.0 : 0.0);
    Style.foreground(Created, Look.Color);
    return Created;
}

Node Builder::button(Node Parent, const Pack &Layout, std::string_view Text, const char *Icon,
                     std::function<void()> OnClick) const {
    const Node Created = Parent.add(Node::create("button"), Layout);
    Created.str("text", Text);
    if (Icon != nullptr) {
        Created.str("icon", Icon);
    }
    Router.on_click(Created, std::move(OnClick));
    return Created;
}

Node Builder::checkbox(Node Parent, const Pack &Layout, std::string_view Text, bool Checked,
                       std::function<void(bool)> OnChange) const {
    const Node Created = Parent.add(Node::create("checkbox"), Layout);
    Created.str("label", Text);
    Created.num("value", Checked ? 1.0 : 0.0);
    Router.on_change(Created, [Created, Notify = std::move(OnChange)] { Notify(Created.number("value") != 0.0); });
    return Created;
}

Node Builder::text_field(Node Parent, const Pack &Layout, std::string_view Value,
                         std::function<void(const std::string &)> OnChange) const {
    const Node Created = Parent.add(Node::create("text"), Layout);
    Created.str("value", Value);
    Created.num("live", 1.0);
    Router.on_change(Created, [Created, Notify = std::move(OnChange)] { Notify(Created.string("value")); });
    return Created;
}

} // namespace scriptroom::gui
