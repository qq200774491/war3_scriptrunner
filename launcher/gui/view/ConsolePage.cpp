#include "ConsolePage.h"

namespace scriptroom::gui {

ConsolePage::ConsolePage(const ViewServices &Context, Node Parent)
    : Services(Context), Log([&Model = Context.Model](const std::string &Line) { Model.select_log(Line); }) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    Body = Ui.frame(Parent, {.Fill = Stretch::Both, .Expand = true}, std::nullopt, 16);

    const Node Title = Ui.frame(Body, {.Side = Edge::Top, .Fill = Stretch::X});
    Ui.label(Title, {.Side = Edge::Left}, "云脚本", {.Scale = TitleText, .Bold = true});
    Ui.label(Title, {.Side = Edge::Left, .PadLeft = 6}, "日志", {.Color = &Palette::Muted, .Scale = SmallText});
    Ui.button(Title, {.Side = Edge::Right}, "清空显示", "TRASH", [&Model] { Model.clear_logs(); });
    FollowCheck = Ui.checkbox(Title, {.Side = Edge::Right, .PadRight = 12}, "跟随", Model.follow(),
                              [&Model](bool On) { Model.set_follow(On); });

    Ui.divider(Body, {.Side = Edge::Top, .Fill = Stretch::X, .PadTop = 10, .PadBottom = 10});

    Body.add(Log.node(), {.Fill = Stretch::Both, .Expand = true});
    Ui.style().watch([this](const Palette &Colors) { Log.set_palette(Colors); });

    EmptyState = Ui.frame(Body, {.Fill = Stretch::Both, .Expand = true});
    Ui.label(EmptyState, {.PadTop = 80}, "", {.Color = &Palette::Muted, .Scale = 2.4, .Align = "center"})
        .str("icon", "CONSOLE");
    Ui.label(EmptyState, {.PadTop = 12}, "控制台已就绪", {.Scale = 1.2});
    Ui.label(EmptyState, {.PadTop = 4}, "启动房间后，云脚本输出会显示在这里", {.Color = &Palette::Muted, .Scale = SmallText});
    Ui.button(EmptyState, {.PadTop = 18}, "检查路径配置", "PREFERENCES", [&Model] { Model.show_page(Page::Settings); });
}

void ConsolePage::sync() {
    const AppModel &Model = Services.Model;
    const bool HasLogs = !Model.logs().empty();
    Log.node().set_visible(HasLogs);
    EmptyState.set_visible(!HasLogs);
    FollowCheck.set_value(Model.follow() ? 1.0 : 0.0);
    Log.show(Model.logs(), Model.filter(), Model.selected_log(), Model.follow());
}

} // namespace scriptroom::gui
