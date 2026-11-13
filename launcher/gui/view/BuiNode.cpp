#include "BuiNode.h"

#include "Error.h"

#include <cmath>
#include <format>

namespace scriptroom::gui {

int px(int Logical) {
    return static_cast<int>(std::lround(static_cast<double>(Logical) * static_cast<double>(BK_scale())));
}

Node Node::create(const char *Type) {
    BKNode *Raw = BK_node_new(Type);
    require(Raw != nullptr, std::format("BUI 不支持节点类型 {}", Type));
    return Node(Raw);
}

Node Node::add(Node Child, const Pack &Layout) const {
    BK_node_add(Handle, Child.Handle, -1);
    Child.set_pack(Layout);
    return Child;
}

void Node::set_pack(const Pack &Layout) const {
    BK_geom_set_manager(Handle, BK_GEOM_PACK);
    BK_geom_set(Handle, "side", static_cast<double>(Layout.Side));
    BK_geom_set(Handle, "fill", static_cast<double>(Layout.Fill));
    BK_geom_set(Handle, "expand", Layout.Expand ? 1.0 : 0.0);
    BK_geom_set(Handle, "padx0", px(Layout.PadLeft));
    BK_geom_set(Handle, "padx1", px(Layout.PadRight));
    BK_geom_set(Handle, "pady0", px(Layout.PadTop));
    BK_geom_set(Handle, "pady1", px(Layout.PadBottom));
    BK_geom_set(Handle, "reqwidth", Layout.Width > 0 ? px(Layout.Width) : 0);
    BK_geom_set(Handle, "reqheight", Layout.Height > 0 ? px(Layout.Height) : 0);
}

const Node &Node::num(const char *Key, double Value) const {
    BK_set_num(Handle, Key, Value);
    return *this;
}

const Node &Node::str(const char *Key, std::string_view Value) const {
    BK_set_str(Handle, Key, std::string(Value).c_str());
    return *this;
}

double Node::number(const char *Key) const {
    return BK_get_num(Handle, Key);
}

std::string Node::string(const char *Key) const {
    return BK_get_str(Handle, Key);
}

const Node &Node::set_text(std::string_view Text) const {
    if (string("text") != Text) {
        str("text", Text);
    }
    return *this;
}

const Node &Node::set_visible(bool Shown) const {
    if ((number("visible") != 0.0) != Shown) {
        num("visible", Shown ? 1.0 : 0.0);
    }
    return *this;
}

const Node &Node::set_enabled(bool On) const {
    if ((number("enabled") != 0.0) != On) {
        num("enabled", On ? 1.0 : 0.0);
    }
    return *this;
}

const Node &Node::set_value(double Value) const {
    if (number("value") != Value) {
        num("value", Value);
    }
    return *this;
}

const Node &Node::text_color(Rgb Color, int Alpha) const {
    const std::uint32_t Argb = (static_cast<std::uint32_t>(Alpha) << 24) | (static_cast<std::uint32_t>(Color.R) << 16) |
                               (static_cast<std::uint32_t>(Color.G) << 8) | Color.B;
    return num("text_color", static_cast<double>(Argb));
}

const Node &Node::background(Rgb Color, float Alpha) const {
    float Rgba[4];
    to_rgba(Color, Alpha, Rgba);
    BK_set_color(Handle, "bg", Rgba);
    return *this;
}

const Node &Node::no_background() const {
    const float Clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    BK_set_color(Handle, "bg", Clear);
    return *this;
}

NodeTree::~NodeTree() {
    BK_node_free(Root.raw());
}

void to_rgba(Rgb Color, float Alpha, float Out[4]) {
    Out[0] = static_cast<float>(Color.R) / 255.0f;
    Out[1] = static_cast<float>(Color.G) / 255.0f;
    Out[2] = static_cast<float>(Color.B) / 255.0f;
    Out[3] = Alpha;
}

} // namespace scriptroom::gui
