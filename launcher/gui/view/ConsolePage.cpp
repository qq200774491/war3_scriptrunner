#include "ConsolePage.h"

#include "Geometry.h"

namespace scriptroom::gui {

ConsolePage::ConsolePage(const ViewServices &Context, Node Parent)
    : Services(Context), Log([&Model = Context.Model, &Events = Context.Ui.router()](const std::string &Line) {
          Events.invoke([&Model, &Line] { Model.select_log(Line); });
      }) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    Body = Ui.frame(Parent, {.Fill = Stretch::Both, .Expand = true});

    const Node Toolbar = Ui.frame(Body, {.Side = Edge::Top, .Fill = Stretch::X, .Height = geometry::ToolbarHeight});
    Ui.label(Toolbar, {.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = 20}, "云脚本日志",
             {.Scale = TitleText, .Bold = true});
    Ui.pill(Toolbar, {.Side = Edge::Right, .PadRight = 16}, "清空显示", Tone::Secondary, [&Model] { Model.clear_logs(); });
    CopyLine = Ui.pill(Toolbar, {.Side = Edge::Right, .PadRight = 4}, "复制选中行", Tone::Secondary, [&Model, this] {
        if (const auto &Line = Model.selected_log()) {
            BK_clipboard_set(BK_node_context(Body.raw()), Line->c_str());
        }
    });
    FollowCheck = Ui.checkbox(Toolbar, {.Side = Edge::Right, .PadRight = 12}, "跟随", Model.follow(),
                              [&Model](bool On) { Model.set_follow(On); });

    Body.add(Log.node(), {.Fill = Stretch::Both, .Expand = true});
    Ui.style().watch([this](const Palette &Colors) { Log.set_palette(Colors); });

    EmptyState = Ui.frame(Body, {.Fill = Stretch::Both, .Expand = true});
    Ui.label(EmptyState, {.PadTop = 110}, "", {.Color = &Palette::Text4, .Scale = 2.4, .Align = "center"})
        .str("icon", "CONSOLE");
    Ui.label(EmptyState, {.PadTop = 16}, "控制台已就绪", {.Scale = TitleText});
    Ui.label(EmptyState, {.PadTop = 8}, "启动房间后，云脚本输出会显示在这里", {.Color = &Palette::Text3});
    Ui.pill(EmptyState, {.PadTop = 20}, "检查路径配置", Tone::Secondary, [&Model] { Model.show_page(Page::Settings); });
}

void ConsolePage::sync() {
    const AppModel &Model = Services.Model;
    const bool HasLogs = !Model.logs().empty();
    Log.node().set_visible(HasLogs);
    EmptyState.set_visible(!HasLogs);
    FollowCheck.set_value(Model.follow() ? 1.0 : 0.0);
    CopyLine.set_enabled(Model.selected_log().has_value());
    Log.show(Model.logs(), Model.filter(), Model.selected_log(), Model.follow());
}

} // namespace scriptroom::gui
