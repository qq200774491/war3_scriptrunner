#include "ActionBar.h"

namespace scriptroom::gui {

ActionBar::ActionBar(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    const Node Bar = Ui.frame(Parent, {.Side = Edge::Top, .Fill = Stretch::X}, &Palette::Panel, 10);
    Ui.divider(Parent, {.Side = Edge::Top, .Fill = Stretch::X});

    // BUI 的普通按钮没有强调色；常亮的开关使用主题的“选中”配色，正好是强调色。
    // 开关被点击时会翻转自己的值，所以每次都立即复位成常亮。
    StartButton = Bar.add(Node::create("toggle"), {.Side = Edge::Left, .PadRight = 8, .Width = 96, .Height = 28});
    StartButton.str("label", "启动");
    StartButton.num("value", 1.0);
    // BUI 的图标是固定的白色，不随文字色变化：强调色是浅灰（黑白灰主题）时白色图标看不清，就不画图标。
    Ui.style().watch([Button = StartButton](const Palette &Colors) {
        const bool DarkText = Colors.OnAccent == Rgb{0, 0, 0};
        Button.str("icon", DarkText ? "" : "PLAY");
    });
    Ui.router().on_change(StartButton, [&Model, Button = StartButton] {
        Button.num("value", 1.0);
        Model.start(SessionMode::Run);
    });

    CheckButton = Ui.button(Bar, {.Side = Edge::Left, .PadRight = 8, .Width = 86, .Height = 28}, "检查", "CHECKMARK",
                            [&Model] { Model.start(SessionMode::Check); });
    CheckButton.str("description", "检查配置及运行组件");
    StopButton = Ui.button(Bar, {.Side = Edge::Left, .Width = 86, .Height = 28}, "停止", "CANCEL",
                           [&Model] { Model.stop(); });
    StopButton.str("description", "停止云服务");

    CloseGamesButton = Ui.button(Bar, {.Side = Edge::Right, .Width = 140, .Height = 28}, "关闭全部魔兽", "PANEL_CLOSE",
                                 [&Model] { Model.close_games(); });
    CloseGamesButton.str("description", "请求所有魔兽正常退出；仍未退出时可强制结束");
}

void ActionBar::sync() {
    const AppModel &Model = Services.Model;
    StartButton.set_enabled(!Model.running());
    CheckButton.set_enabled(!Model.running());
    StopButton.set_enabled(Model.running());
    CloseGamesButton.set_enabled(!Model.closing_games());
}

} // namespace scriptroom::gui
