#include "AboutPage.h"

#include "Geometry.h"
#include "SystemDialogs.h"

namespace scriptroom::gui {

namespace {

constexpr int LabelWidth = 96;
constexpr const char *HomePage = "https://github.com/qq200774491/war3_mls";

} // namespace

AboutPage::AboutPage(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    Body = Ui.frame(Parent, {.Fill = Stretch::Both, .Expand = true});

    const Node Toolbar = Ui.frame(Body, {.Side = Edge::Top, .Fill = Stretch::X, .Height = geometry::ToolbarHeight});
    Ui.label(Toolbar, {.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = 20}, "关于", {.Scale = TitleText, .Bold = true});

    const Node Content = Ui.frame(Body, {.Fill = Stretch::Both, .Expand = true}, std::nullopt, 24);
    add_row(Content, "软件名称", "云脚本Test启动器（ScriptRoom）");
    add_row(Content, "作者", "花吃了阿九");
    add_row(Content, "参与者", "Carlos、小为（xiaowei）");
    add_row(Content, "版本", SCRIPTROOM_VERSION);
    add_row(Content, "许可", "GPL-3.0，全文见仓库根目录 LICENSE");

    const Node Home = Ui.frame(Content, {.Fill = Stretch::X});
    Ui.label(Home, {.Side = Edge::Left, .Fill = Stretch::Y, .Width = LabelWidth}, "项目主页", {.Color = &Palette::Text3});
    Ui.label(Home, {.Side = Edge::Left, .Fill = Stretch::Y}, HomePage);
    Ui.pill(Home, {.Side = Edge::Left, .PadLeft = 12}, "打开", Tone::Secondary, [] { open_url(HomePage); });
}

void AboutPage::add_row(Node Parent, std::string_view Title, std::string_view Value) const {
    const Builder &Ui = Services.Ui;
    const Node Row = Ui.frame(Parent, {.Fill = Stretch::X, .PadBottom = 14});
    Ui.label(Row, {.Side = Edge::Left, .Width = LabelWidth}, Title, {.Color = &Palette::Text3});
    Ui.label(Row, {.Side = Edge::Left}, Value);
}

} // namespace scriptroom::gui
