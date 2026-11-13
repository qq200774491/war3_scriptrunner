#include "InspectorPanel.h"

#include "Geometry.h"
#include "Text.h"

#include <format>

namespace scriptroom::gui {

namespace {

Node value_row(const Builder &Ui, Node Parent, std::string_view Title) {
    const Node Row = Ui.frame(Parent, {.Fill = Stretch::X, .PadBottom = 6});
    const Node Value = Ui.label(Row, {.Side = Edge::Right}, "", {.Align = "right"});
    Ui.label(Row, {.Side = Edge::Left}, Title, {.Color = &Palette::Text3});
    return Value;
}

} // namespace

InspectorPanel::InspectorPanel(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    Body = Ui.frame(Parent, {.Side = Edge::Right, .Fill = Stretch::Y, .Width = geometry::SideWidth});

    const Node Head = Ui.frame(Body, {.Side = Edge::Top, .Fill = Stretch::X, .Height = geometry::ToolbarHeight});
    Ui.label(Head, {.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = 16}, "详情", {.Scale = TitleText, .Bold = true});
    Ui.icon_button(Head, {.Side = Edge::Right, .PadRight = 12}, "RIGHTARROW", "收起详情",
                   [&Model] { Model.set_inspector_open(false); });

    const Node Foot = Ui.frame(Body, {.Side = Edge::Bottom, .Fill = Stretch::X}, std::nullopt, 16);
    Ui.pill(Foot, {.Fill = Stretch::X}, "编辑配置", Tone::Secondary, [&Model] { Model.show_page(Page::Settings); });

    const Node Scroll = Body.add(Node::create("scroll"), {.Fill = Stretch::Both, .Expand = true});
    const Node Content = Ui.frame(Scroll, {.Fill = Stretch::X}, std::nullopt, 16);

    SelectedSection = Ui.frame(Content, {.Fill = Stretch::X, .PadBottom = 22});
    SelectedSection.add(SelectedText.node(), {.Fill = Stretch::X, .PadBottom = 8});
    Ui.pill(SelectedSection, {.Side = Edge::Top}, "复制这行日志", Tone::Secondary, [&Model, this] {
        if (const auto &Line = Model.selected_log()) {
            BK_clipboard_set(BK_node_context(SelectedSection.raw()), Line->c_str());
        }
    });

    ConfigSection = Ui.frame(Content, {.Fill = Stretch::X});
    add_path(ConfigSection, "地图", MapPath);
    add_path(ConfigSection, "云脚本", ScriptsPath);
    add_path(ConfigSection, "客户端入口", EntryPath);
    add_path(ConfigSection, "存档目录", ArchivesPath);
    DurationValue = value_row(Ui, ConfigSection, "服务时长");
    TimeoutValue = value_row(Ui, ConfigSection, "启动超时");

    Ui.style().watch([this](const Palette &Colors) {
        for (TextBlock *Block : {&SelectedText, &MapPath, &ScriptsPath, &EntryPath, &ArchivesPath}) {
            Block->set_color(Colors.Text);
        }
    });
}

void InspectorPanel::add_path(Node Parent, const char *Title, TextBlock &Value) {
    Services.Ui.label(Parent, {.Fill = Stretch::X, .PadBottom = 2}, Title, {.Color = &Palette::Text3});
    Parent.add(Value.node(), {.Fill = Stretch::X, .PadBottom = 14});
}

void InspectorPanel::sync(const std::optional<Config> &Resolved) {
    const AppModel &Model = Services.Model;
    const auto &Line = Model.selected_log();
    SelectedSection.set_visible(Line.has_value());
    if (Line) {
        SelectedText.set_text(*Line);
    }

    ConfigSection.set_visible(Resolved.has_value());
    if (!Resolved) {
        return;
    }
    MapPath.set_text(path_to_utf8(Resolved->Map));
    ScriptsPath.set_text(path_to_utf8(Resolved->CloudScripts));
    EntryPath.set_text(path_to_utf8(Resolved->LocalEntry));
    ArchivesPath.set_text(path_to_utf8(Resolved->Archives));
    DurationValue.set_text(std::format("{} 分钟", Resolved->Seconds / 60));
    TimeoutValue.set_text(std::format("{} 秒", Resolved->StartupTimeout));
}

} // namespace scriptroom::gui
