#include "InspectorPanel.h"

#include "Text.h"

#include <format>

namespace scriptroom::gui {

namespace {

void section_title(const Builder &Ui, Node Parent, std::string_view Text) {
    Ui.label(Parent, {.Fill = Stretch::X, .PadTop = 18, .PadBottom = 5}, Text,
             {.Color = &Palette::Muted, .Scale = SmallText});
}

/// “名称 ……… 值”：名称靠左，值靠右。
Node value_row(const Builder &Ui, Node Parent, std::string_view Title) {
    const Node Row = Ui.frame(Parent, {.Fill = Stretch::X});
    const Node Value = Ui.label(Row, {.Side = Edge::Right}, "", {.Align = "right"});
    Ui.label(Row, {.Side = Edge::Left}, Title, {.Color = &Palette::Muted});
    return Value;
}

} // namespace

InspectorPanel::InspectorPanel(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    Body = Ui.frame(Parent, {.Side = Edge::Right, .Fill = Stretch::Y, .Width = 256}, &Palette::Panel, 14);

    const Node Header = Ui.frame(Body, {.Fill = Stretch::X, .PadBottom = 12});
    Ui.label(Header, {.Side = Edge::Left, .Fill = Stretch::Y}, "详情", {.Scale = SmallText, .Bold = true});
    Ui.button(Header, {.Side = Edge::Right, .Width = 28}, "", "MENU_PANEL", [&Model] { Model.set_inspector_open(false); })
        .str("description", "收起详情");

    const Node Room = Ui.card(Body, {.Fill = Stretch::X}, 14, &Palette::Canvas);
    Ui.label(Room, {.Side = Edge::Left, .Fill = Stretch::Y, .PadRight = 8}, "", {.Color = &Palette::Accent})
        .str("icon", "SCRIPT");
    const Node Names = Ui.frame(Room, {.Side = Edge::Left});
    Ui.label(Names, {.Fill = Stretch::X}, "云脚本房间", {.Bold = true});
    StatusText = Ui.label(Names, {.Fill = Stretch::X}, "", {.Color = &Palette::Muted, .Scale = SmallText});

    // 配置入口固定在底部，长路径只滚动上面的内容。
    Ui.button(Body, {.Side = Edge::Bottom, .Fill = Stretch::X, .PadTop = 8, .Height = 26}, "编辑配置", "PREFERENCES",
              [&Model] { Model.show_page(Page::Settings); });

    const Node Scroll = Body.add(Node::create("scroll"), {.Fill = Stretch::Both, .Expand = true, .PadTop = 4});
    const Node Content = Ui.frame(Scroll, {.Fill = Stretch::X}, std::nullopt, 0);

    SelectedSection = Ui.frame(Content, {.Fill = Stretch::X});
    section_title(Ui, SelectedSection, "选中日志");
    SelectedSection.add(SelectedText.node(), {.Fill = Stretch::X, .PadBottom = 6});
    Ui.button(SelectedSection, {.Fill = Stretch::X, .Height = 24}, "复制内容", "COPYDOWN", [&Model, this] {
        if (const auto &Line = Model.selected_log()) {
            BK_clipboard_set(BK_node_context(SelectedSection.raw()), Line->c_str());
        }
    });

    ConfigSection = Ui.frame(Content, {.Fill = Stretch::X});
    section_title(Ui, ConfigSection, "运行路径");
    add_path(ConfigSection, "地图", MapPath);
    add_path(ConfigSection, "云脚本", ScriptsPath);
    add_path(ConfigSection, "客户端入口", EntryPath);
    section_title(Ui, ConfigSection, "存档");
    add_path(ConfigSection, "目录", ArchivesPath);
    Ui.label(ConfigSection, {.Fill = Stretch::X}, "玩家 0 / 玩家 1 · 独立 INI", {.Color = &Palette::Muted, .Scale = SmallText});
    section_title(Ui, ConfigSection, "运行设置");
    DurationValue = value_row(Ui, ConfigSection, "服务时长");
    TimeoutValue = value_row(Ui, ConfigSection, "启动超时");

    Ui.style().watch([this](const Palette &Colors) {
        for (TextBlock *Block : {&SelectedText, &MapPath, &ScriptsPath, &EntryPath, &ArchivesPath}) {
            Block->set_color(Colors.Text);
        }
    });
}

void InspectorPanel::add_path(Node Parent, const char *Title, TextBlock &Value) {
    Services.Ui.label(Parent, {.Fill = Stretch::X}, Title, {.Color = &Palette::Muted, .Scale = SmallText});
    Parent.add(Value.node(), {.Fill = Stretch::X, .PadBottom = 8});
}

void InspectorPanel::sync(const std::optional<Config> &Resolved) {
    const AppModel &Model = Services.Model;
    StatusText.set_text(Model.running() ? "任务执行中" : "未启动");

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
