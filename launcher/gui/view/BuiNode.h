#pragma once

#include "Theme.h"

#include "BUI_kit.h"

#include <string>
#include <string_view>

namespace scriptroom::gui {

/// 逻辑像素换算成物理像素（BK_scale，随系统缩放变化）。
int px(int Logical);

/// BUI 的 pack 几何管理器参数（Tk 的 pack）。所有长度都是逻辑像素。
enum class Edge { Top = 0, Bottom = 1, Left = 2, Right = 3 };
enum class Stretch { None = 0, X = 1, Y = 2, Both = 3 };

struct Pack {
    Edge Side = Edge::Top;
    Stretch Fill = Stretch::None;
    bool Expand = false;
    int PadLeft = 0;
    int PadRight = 0;
    int PadTop = 0;
    int PadBottom = 0;
    int Width = 0;  ///< 覆盖自然宽度；0 表示用自然宽度
    int Height = 0; ///< 覆盖自然高度；0 表示用自然高度
};

/// BKNode 的非拥有句柄。节点树的所有权归根节点（NodeTree），Node 只是带名字的便利封装：
/// 句柄在所属 NodeTree 存活期间有效，只能在界面线程使用。
class Node {
public:
    Node() = default;
    explicit Node(BKNode *Raw) : Handle(Raw) {}

    /// 创建指定类型的游离节点；类型未知时抛出 Error。必须随后 add 到父节点或交给 NodeTree。
    static Node create(const char *Type);

    BKNode *raw() const { return Handle; }
    explicit operator bool() const { return Handle != nullptr; }
    bool operator==(const Node &) const = default;

    /// 把子节点加到末尾并按 Layout 摆放；返回子节点便于继续配置。
    Node add(Node Child, const Pack &Layout = {}) const;

    const Node &num(const char *Key, double Value) const;
    const Node &str(const char *Key, std::string_view Value) const;
    double number(const char *Key) const;
    std::string string(const char *Key) const;

    /// 仅在内容变化时才写入，避免无谓的重排与重绘。
    const Node &set_text(std::string_view Text) const;
    const Node &set_visible(bool Shown) const;
    const Node &set_enabled(bool On) const;
    const Node &set_value(double Value) const;

    /// 文字颜色（标签、按钮）；Alpha 取 0..255。
    const Node &text_color(Rgb Color, int Alpha = 255) const;
    /// 框架背景色；Alpha 为 0 表示透明。
    const Node &background(Rgb Color, float Alpha = 1.0f) const;
    const Node &no_background() const;

    void set_pack(const Pack &Layout) const;

private:
    BKNode *Handle = nullptr;
};

/// 根节点的所有者：析构时释放整棵节点树。
class NodeTree {
public:
    NodeTree() = default;
    explicit NodeTree(Node Top) : Root(Top) {}
    ~NodeTree();
    NodeTree(const NodeTree &) = delete;
    NodeTree &operator=(const NodeTree &) = delete;

    Node root() const { return Root; }

private:
    Node Root;
};

/// Rgb 转成 BUI 使用的 RGBA（0..1）。
void to_rgba(Rgb Color, float Alpha, float Out[4]);

} // namespace scriptroom::gui
