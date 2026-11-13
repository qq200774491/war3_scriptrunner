#include "DialogLayer.h"

#include <format>

namespace scriptroom::gui {

DialogLayer::DialogLayer(const ViewServices &Context, Node Root) : Services(Context) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;

    ForceOverlay = make_overlay(Root);
    const Node Force = make_card(ForceOverlay, "仍有魔兽未退出");
    Force.add(ForceSummary.node(), {.Fill = Stretch::X, .PadBottom = 6});
    Force.add(ForcePids.node(), {.Fill = Stretch::X, .PadBottom = 16});
    const Node ForceButtons = Ui.frame(Force, {.Fill = Stretch::X});
    Ui.pill(ForceButtons, {.Side = Edge::Right}, "强制结束", Tone::Danger, [&Model] { Model.confirm_force_close(); });
    Ui.pill(ForceButtons, {.Side = Edge::Right, .PadRight = 8}, "取消", Tone::Secondary,
            [&Model] { Model.cancel_force_close(); });

    ExitOverlay = make_overlay(Root);
    const Node Exit = make_card(ExitOverlay, "退出云脚本Test启动器");
    Exit.add(ExitMessage.node(), {.Fill = Stretch::X, .PadBottom = 16});
    const Node ExitButtons = Ui.frame(Exit, {.Fill = Stretch::X});
    Ui.pill(ExitButtons, {.Side = Edge::Right}, "停止并退出", Tone::Danger, [&Model] { Model.stop_and_exit(); });
    Ui.pill(ExitButtons, {.Side = Edge::Right, .PadRight = 8}, "继续运行", Tone::Secondary,
            [&Model] { Model.keep_running(); });
    ExitMessage.set_text("云服务正在运行，退出会中断本轮云脚本。");

    Ui.style().watch([this](const Palette &Colors) {
        for (TextBlock *Block : {&ForceSummary, &ForcePids, &ExitMessage}) {
            Block->set_color(Colors.Text);
        }
    });
}

Node DialogLayer::make_overlay(Node Root) const {
    const Node Overlay = Node::create("frame");
    BK_node_add(Root.raw(), Overlay.raw(), -1);
    BK_geom_set_manager(Overlay.raw(), BK_GEOM_PLACE);
    BK_geom_set(Overlay.raw(), "relwidth", 1.0);
    BK_geom_set(Overlay.raw(), "relheight", 1.0);
    Overlay.num("events", 1.0);
    Overlay.background(Rgb{0, 0, 0}, 0.5f);
    Overlay.num("visible", 0.0);
    return Overlay;
}

Node DialogLayer::make_card(Node Overlay, std::string_view Title) const {
    const Node Card = Services.Ui.card(Overlay, {.Expand = true, .Width = 400}, 22, &Palette::Popup, 9);
    Services.Ui.label(Card, {.Fill = Stretch::X, .PadBottom = 12}, Title, {.Scale = TitleText, .Bold = true});
    return Card;
}

void DialogLayer::sync() {
    const AppModel &Model = Services.Model;
    ForceOverlay.set_visible(Model.awaiting_force_close());
    ExitOverlay.set_visible(Model.exit_confirmation_open());
    if (!Model.awaiting_force_close()) {
        return;
    }
    const std::vector<std::uint32_t> Pids = Model.force_close_pids();
    if (Pids == ShownPids) {
        return;
    }
    ShownPids = Pids;
    std::string List;
    for (const std::uint32_t Pid : Pids) {
        List += (List.empty() ? "" : "、") + std::to_string(Pid);
    }
    ForceSummary.set_text(std::format("准备强制结束 {} 个进程。未保存的进度会丢失。", Pids.size()));
    ForcePids.set_text(List);
}

} // namespace scriptroom::gui
