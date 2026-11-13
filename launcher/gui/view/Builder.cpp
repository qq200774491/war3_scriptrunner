#include "Builder.h"

#include "Geometry.h"
#include "Paint.h"

namespace scriptroom::gui {

Node Builder::frame(Node Parent, const Pack &Layout, std::optional<ColorRole> Background, int Padding) const {
    const Node Created = Parent.add(Node::create("frame"), Layout);
    Created.num("padding", px(Padding));
    if (Background) {
        Style.background(Created, *Background);
    }
    return Created;
}

Node Builder::card(Node Parent, const Pack &Layout, int Padding, ColorRole Fill, int Radius) const {
    const Node Outer = frame(Parent, Layout);
    backdrop(Outer, [this, Fill, Radius](BuiRaster *Target, const Surface::Box &Area, const Surface &) {
        paint::round_rect(Target, Area.X, Area.Y, Area.W, Area.H, static_cast<float>(px(Radius)),
                          Style.palette().*Fill);
    });
    return frame(Outer, {.Fill = Stretch::Both, .Expand = true}, std::nullopt, Padding);
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

Node Builder::icon_button(Node Parent, const Pack &Layout, const char *Icon, std::string_view Tooltip,
                          std::function<void()> OnClick) const {
    Pack Shape = Layout;
    Shape.Width = Shape.Width > 0 ? Shape.Width : geometry::ButtonHeight;
    Shape.Height = Shape.Height > 0 ? Shape.Height : geometry::ButtonHeight;
    const Node Created = Parent.add(Node::create("button"), Shape);
    Created.str("text", "");
    Created.str("icon", Icon);
    Created.num("emboss", 0.0);
    Created.str("description", Tooltip);
    Router.on_click(Created, std::move(OnClick));
    return Created;
}

PillButton Builder::pill(Node Parent, const Pack &Layout, std::string_view Text, Tone Kind,
                         std::function<void()> OnClick) const {
    const PillButton Button(Style, std::string(Text), Kind,
                            routed([Click = std::move(OnClick)](const Surface::Pointer &) { Click(); }));
    Pack Shape = Layout;
    Shape.Width = Shape.Width > 0 ? Shape.Width : Button.natural_width();
    Shape.Height = geometry::ButtonHeight;
    Parent.add(Button.node(), Shape);
    Style.watch([Handle = Button.node()](const Palette &) { BK_node_invalidate(Handle.raw()); });
    return Button;
}

Surface *Builder::surface(Node Parent, const Pack &Layout, Surface::Painter Paint,
                          std::function<void(const Surface::Pointer &)> OnClick) const {
    Surface *Area = Surface::create(std::move(Paint), routed(std::move(OnClick)));
    Parent.add(Area->node(), Layout);
    repaint_on_theme(Area);
    return Area;
}

Surface *Builder::backdrop(Node Parent, Surface::Painter Paint,
                           std::function<void(const Surface::Pointer &)> OnClick) const {
    Surface *Area = Surface::create(std::move(Paint), routed(std::move(OnClick)));
    Area->fill_parent(Parent);
    repaint_on_theme(Area);
    return Area;
}

Surface::ClickHandler Builder::routed(std::function<void(const Surface::Pointer &)> OnClick) const {
    if (!OnClick) {
        return {};
    }
    return [&Events = Router, Click = std::move(OnClick)](const Surface::Pointer &State) {
        Events.invoke([&Click, &State] { Click(State); });
    };
}

void Builder::repaint_on_theme(const Surface *Area) const {
    Style.watch([Area](const Palette &) { Area->invalidate(); });
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
