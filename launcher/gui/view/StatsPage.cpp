#include "StatsPage.h"

#include "Geometry.h"
#include "Paint.h"

#include <format>
#include <string>

namespace scriptroom::gui {

namespace {

constexpr int TableRow = 30;
constexpr int TableGap = 22;
constexpr int LatencyHeight = 170;
constexpr int SmallHeight = 130;
constexpr int TitleHeight = 28;
constexpr int CellGap = 24;
constexpr int StackBelow = 720; ///< 主区域窄于它时下面两张图上下叠放
constexpr int RangeOption = 64;
constexpr int NameColumn = 120;

constexpr std::array<const char *, 6> TableHeads = {"玩家", "请求", "失败", "平均", "P95", "最大"};

void place(Node Target, double RelX, double RelY, double RelW, double RelH, int X, int Y, int W, int H) {
    BKNode *Raw = Target.raw();
    BK_geom_set_manager(Raw, BK_GEOM_PLACE);
    BK_geom_set(Raw, "relx", RelX);
    BK_geom_set(Raw, "rely", RelY);
    BK_geom_set(Raw, "relwidth", RelW);
    BK_geom_set(Raw, "relheight", RelH);
    BK_geom_set(Raw, "x", px(X));
    BK_geom_set(Raw, "y", px(Y));
    BK_geom_set(Raw, "width", px(W));
    BK_geom_set(Raw, "height", px(H));
}

/// 停止后“当前每秒多少”没有意义，改看整轮的平均。
double shown_rate(const TimeSeries &Series, std::uint64_t Total, const RunStatsSnapshot &Now) {
    if (Now.Running) {
        return Series.ago(0).value_or(0.0);
    }
    const double Seconds = std::chrono::duration<double>(Now.Uptime).count();
    return Seconds > 0.0 ? static_cast<double>(Total) / Seconds : 0.0;
}

} // namespace

StatsPage::StatsPage(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    Body = Ui.frame(Parent, {.Fill = Stretch::Both, .Expand = true});
    build_toolbar(Ui.frame(Body, {.Side = Edge::Top, .Fill = Stretch::X, .Height = geometry::ToolbarHeight}));

    Empty = Ui.frame(Body, {.Fill = Stretch::Both, .Expand = true});
    Ui.label(Empty, {.PadTop = 110}, "", {.Color = &Palette::Text4, .Scale = 2.4, .Align = "center"})
        .str("icon", "SEQ_HISTOGRAM");
    Ui.label(Empty, {.PadTop = 16}, "服务启动后显示统计", {.Color = &Palette::Text3});

    Content = Body.add(Node::create("scroll"), {.Fill = Stretch::Both, .Expand = true});
    const Node Inner = Ui.frame(Content, {.Fill = Stretch::X}, std::nullopt, 16);
    build_metrics(Inner);
    Table = Ui.surface(Inner, {.Fill = Stretch::X, .PadBottom = TableGap, .Height = TableRow},
                       [this](BuiRaster *Target, const Surface::Box &Area, const Surface &) { paint_table(Target, Area); });

    Ui.label(Inner, {.Fill = Stretch::X, .Height = TitleHeight}, "响应耗时", {.Bold = true});
    Latency = std::make_unique<TrendChart>(Services, Inner, Pack{.Fill = Stretch::X, .PadBottom = TableGap,
                                                                 .Height = LatencyHeight},
                                           TrendChart::Kind::Latency);

    Bottom = Ui.frame(Inner, {.Fill = Stretch::X, .Height = SmallHeight + TitleHeight});
    constexpr std::array<const char *, 2> Titles = {"事件与日志", "宿主队列积压"};
    for (std::size_t Index = 0; Index < BottomCells.size(); ++Index) {
        BottomCells[Index] = Ui.frame(Bottom, {});
        Ui.label(BottomCells[Index], {.Fill = Stretch::X, .Height = TitleHeight}, Titles[Index], {.Bold = true});
    }
    const Pack Chart{.Fill = Stretch::Both, .Expand = true};
    Rates = std::make_unique<TrendChart>(Services, BottomCells[0], Chart, TrendChart::Kind::Rates);
    Backlog = std::make_unique<TrendChart>(Services, BottomCells[1], Chart, TrendChart::Kind::Backlog);
    layout_bottom(false);
}

void StatsPage::build_toolbar(Node Toolbar) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    Ui.label(Toolbar, {.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = 20}, "运行统计",
             {.Scale = TitleText, .Bold = true});
    Pause = Ui.pill(Toolbar, {.Side = Edge::Right, .PadRight = 16}, "暂停刷新", Tone::Secondary,
                    [&Model] { Model.set_stats_paused(!Model.stats_paused()); });
    Ranges = Ui.surface(
        Toolbar,
        {.Side = Edge::Right, .PadRight = 12, .Width = RangeOption * static_cast<int>(AllTrendRanges.size()),
         .Height = geometry::ButtonHeight},
        [this](BuiRaster *Target, const Surface::Box &Area, const Surface &) { paint_ranges(Target, Area); },
        [this](const Surface::Pointer &Where) { pick_range(Where); });
}

void StatsPage::build_metrics(Node Parent) {
    const Builder &Ui = Services.Ui;
    const Node Row = Ui.frame(Parent, {.Fill = Stretch::X, .PadBottom = 18, .Height = 56});
    for (std::size_t Index = 0; Index < Metrics.size(); ++Index) {
        const Node Cell = Ui.frame(Row, {});
        place(Cell, 0.25 * static_cast<double>(Index), 0.0, 0.25, 1.0, 0, 0, 0, 0);
        Metrics[Index].Caption = Ui.label(Cell, {.Fill = Stretch::X}, "", {.Color = &Palette::Text3});
        Metrics[Index].Value = Ui.label(Cell, {.Fill = Stretch::X, .PadTop = 6}, "", {.Scale = 1.4, .Bold = true});
    }
}

void StatsPage::layout_bottom(bool Stack) {
    Stacked = Stack;
    const int Cell = SmallHeight + TitleHeight;
    Bottom.set_pack({.Fill = Stretch::X, .Height = Stacked ? Cell * 2 + CellGap : Cell});
    if (Stacked) {
        place(BottomCells[0], 0.0, 0.0, 1.0, 0.0, 0, 0, 0, Cell);
        place(BottomCells[1], 0.0, 0.0, 1.0, 0.0, 0, Cell + CellGap, 0, Cell);
    } else {
        place(BottomCells[0], 0.0, 0.0, 0.5, 1.0, 0, 0, -CellGap / 2, 0);
        place(BottomCells[1], 0.5, 0.0, 0.5, 1.0, CellGap / 2, 0, -CellGap / 2, 0);
    }
}

void StatsPage::resize(int Width) {
    const bool Narrow = Width < px(StackBelow);
    if (Narrow != Stacked) {
        layout_bottom(Narrow);
    }
}

void StatsPage::paint_table(BuiRaster *Target, const Surface::Box &Area) const {
    const Palette &Colors = Services.Ui.style().palette();
    const RunStatsSnapshot &Now = Services.Model.stats().latest();
    const float Row = static_cast<float>(px(TableRow));
    const float Name = static_cast<float>(px(NameColumn));
    const float Column = (Area.W - Name) / static_cast<float>(TableHeads.size() - 1);
    const float Inset = static_cast<float>(px(16));
    const auto cell_x = [&](std::size_t Index) { return Area.X + Name + Column * static_cast<float>(Index - 1); };

    paint::text(Target, Area.X, Area.Y, Name, Row, TableHeads[0], Colors.Text3);
    for (std::size_t Index = 1; Index < TableHeads.size(); ++Index) {
        paint::text(Target, cell_x(Index), Area.Y, Column - Inset, Row, TableHeads[Index], Colors.Text3,
                    paint::Align::Right);
    }
    const float Radius = static_cast<float>(px(4));
    for (std::size_t Player = 0; Player < Now.Players.size(); ++Player) {
        const PlayerStats &Stats = Now.Players[Player];
        const float Y = Area.Y + Row * static_cast<float>(Player + 1);
        paint::disc(Target, Area.X + Radius, Y + Row * 0.5f, Radius, Colors.Series[Player % Colors.Series.size()]);
        paint::text(Target, Area.X + Radius * 2.0f + static_cast<float>(px(8)), Y, Name, Row,
                    std::format("玩家 {}", Player), Colors.Text);
        const std::array<std::string, 5> Values = {
            std::to_string(Stats.Latency.Count), std::to_string(Stats.Failures), format_ms(Stats.Latency.AverageMs),
            format_ms(Stats.Latency.P95Ms), format_ms(Stats.Latency.MaxMs)};
        for (std::size_t Index = 0; Index < Values.size(); ++Index) {
            const bool Failed = Index == 1 && Stats.Failures > 0;
            paint::mono_text(Target, cell_x(Index + 1), Y, Column - Inset, Row, Values[Index],
                             Failed ? Colors.Danger : Colors.Text, paint::Align::Right);
        }
    }
}

void StatsPage::paint_ranges(BuiRaster *Target, const Surface::Box &Area) const {
    const Palette &Colors = Services.Ui.style().palette();
    const float Option = Area.W / static_cast<float>(AllTrendRanges.size());
    for (std::size_t Index = 0; Index < AllTrendRanges.size(); ++Index) {
        const bool Chosen = AllTrendRanges[Index] == Services.Model.stats_range();
        const float X = Area.X + Option * static_cast<float>(Index);
        if (Chosen) {
            paint::round_rect(Target, X, Area.Y, Option, Area.H, Area.H * 0.5f, Colors.Overlay, 0.12f);
        }
        paint::text(Target, X, Area.Y, Option, Area.H, range_label(AllTrendRanges[Index]),
                    Chosen ? Colors.Text : Colors.Text3, paint::Align::Center);
    }
}

void StatsPage::pick_range(const Surface::Pointer &Where) const {
    int Rect[4] = {};
    BK_node_rect(Ranges->node().raw(), Rect);
    if (Rect[2] <= 0) {
        return;
    }
    const auto Index = static_cast<std::size_t>(Where.X * static_cast<float>(AllTrendRanges.size()) /
                                                static_cast<float>(Rect[2]));
    if (Index < AllTrendRanges.size()) {
        Services.Model.set_stats_range(AllTrendRanges[Index]);
    }
}

std::uint64_t StatsPage::data_stamp() const {
    const AppModel &Model = Services.Model;
    const StatsBoard &Board = Model.stats();
    std::uint64_t Result = Board.event_rate().size();
    const auto mix = [&Result](std::uint64_t Value) { Result = Result * 1000003u + Value; };
    mix(static_cast<std::uint64_t>(Board.latest().Uptime.count()));
    mix(Board.latest().Players.size());
    mix(Board.latest().Running ? 1 : 0);
    mix(static_cast<std::uint64_t>(Model.stats_range()));
    mix(Model.stats_paused() ? 1 : 0);
    for (std::size_t Player = 0; Player < Board.latest().Players.size(); ++Player) {
        mix(Model.player_hidden(Player) ? 1 : 0);
    }
    return Result;
}

void StatsPage::sync() {
    const AppModel &Model = Services.Model;
    const StatsBoard &Board = Model.stats();
    Pause.set_label(Model.stats_paused() ? "继续刷新" : "暂停刷新");
    Pause.node().set_visible(Board.has_data() || Model.stats_paused());
    Ranges->node().set_visible(Board.has_data());
    Empty.set_visible(!Board.has_data());
    Content.set_visible(Board.has_data());
    if (!Board.has_data()) {
        return;
    }
    const RunStatsSnapshot &Now = Board.latest();
    Metrics[0].Caption.set_text(Now.Running ? "运行时长" : "运行时长 · 已停止");
    Metrics[0].Value.set_text(format_uptime(Now.Uptime));
    Metrics[1].Caption.set_text(std::format("房间事件 · 共 {}", Now.RoomEvents));
    Metrics[1].Value.set_text(format_rate(shown_rate(Board.event_rate(), Now.RoomEvents, Now)));
    Metrics[2].Caption.set_text(std::format("日志 · 共 {} 行", Now.LogLines));
    Metrics[2].Value.set_text(format_rate(shown_rate(Board.log_rate(), Now.LogLines, Now)));
    Metrics[3].Caption.set_text(std::format("宿主积压 · 峰值 {}", Now.PeakBacklog));
    Metrics[3].Value.set_text(std::to_string(Now.Backlog));

    if (TableRows != Now.Players.size()) {
        TableRows = Now.Players.size();
        Table->node().set_pack({.Fill = Stretch::X,
                                .PadBottom = TableGap,
                                .Height = TableRow * static_cast<int>(TableRows + 1)});
    }
    const std::uint64_t Next = data_stamp();
    if (Next != Stamp) {
        Stamp = Next;
        Table->invalidate();
        Ranges->invalidate();
        Latency->refresh();
        Rates->refresh();
        Backlog->refresh();
    }
}

} // namespace scriptroom::gui
