#include "ActionBar.h"

#include "Geometry.h"
#include "Paint.h"

namespace scriptroom::gui {

namespace {

constexpr int PairButtonWidth = 112;
constexpr int PanelInset = 12;

}

ActionBar::ActionBar(const ViewServices &Context, Node Head, Node Body) : Services(Context) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;

    StartButton = Ui.pill(Head, {.Side = Edge::Left, .PadLeft = PanelInset, .Width = PairButtonWidth}, "启动",
                          Tone::Primary, [&Model] { Model.start(SessionMode::Run); });
    CheckButton = Ui.pill(Head, {.Side = Edge::Left, .PadLeft = 8, .Width = PairButtonWidth}, "检查", Tone::Secondary,
                          [&Model] { Model.start(SessionMode::Check); });
    CheckButton.node().str("description", "检查配置及运行组件");

    Status = Ui.surface(Body, {.Side = Edge::Top, .Fill = Stretch::X, .Height = 34},
                        [this](BuiRaster *Target, const Surface::Box &Area, const Surface &) {
                            paint_status(Target, Area);
                        });

    const Node Row = Ui.frame(Body, {.Side = Edge::Top, .Fill = Stretch::X, .PadTop = 6});
    StopButton = Ui.pill(Row, {.Side = Edge::Left, .Width = PairButtonWidth}, "停止", Tone::Secondary,
                         [&Model] { Model.stop(); });
    StopButton.node().str("description", "停止云服务");
    CloseGamesButton = Ui.pill(Row, {.Side = Edge::Left, .PadLeft = 8, .Width = PairButtonWidth}, "关闭魔兽",
                               Tone::Danger, [&Model] { Model.close_games(); });
    CloseGamesButton.node().str("description", "请求所有魔兽正常退出；仍未退出时可强制结束");
}

void ActionBar::paint_status(BuiRaster *Target, const Surface::Box &Area) const {
    const Palette &P = Services.Ui.style().palette();
    const AppModel &Model = Services.Model;
    const bool Running = Model.running();
    const float Dot = static_cast<float>(px(4));
    const float DotX = Area.X + static_cast<float>(px(16));
    paint::disc(Target, DotX, Area.Y + Area.H * 0.5f, Dot, Running ? P.Success : P.Text4);
    const float TextX = DotX + static_cast<float>(px(12));
    paint::text(Target, TextX, Area.Y, Area.W - (TextX - Area.X), Area.H, "云脚本房间", P.Text);

    std::string State = "未启动";
    Rgb StateColor = P.Text3;
    if (Model.closing_games()) {
        State = "正在关闭魔兽";
        StateColor = P.AccentOn;
    } else if (Running) {
        State = "运行中";
        StateColor = P.Success;
    }
    paint::text(Target, Area.X, Area.Y, Area.W - static_cast<float>(px(16)), Area.H, State, StateColor,
                paint::Align::Right);
}

void ActionBar::sync() {
    const AppModel &Model = Services.Model;
    StartButton.set_enabled(!Model.running());
    CheckButton.set_enabled(!Model.running());
    StopButton.set_enabled(Model.running());
    CloseGamesButton.set_enabled(!Model.closing_games());
    Status->invalidate();
}

} // namespace scriptroom::gui
