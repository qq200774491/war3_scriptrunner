#include "TrendChart.h"

#include "Paint.h"
#include "StatsChart.h"

#include <algorithm>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace scriptroom::gui {

namespace {

constexpr int LegendHeight = 26;
constexpr int Gutter = 72; ///< 绘图区左边留给刻度文字
constexpr int TipRow = 22;
constexpr int Dot = 4;

void dashed_line(BuiRaster *Target, float Left, float Right, float Y, Rgb Color) {
    const float Dash = static_cast<float>(px(6));
    const float Gap = static_cast<float>(px(4));
    for (float X = Left; X < Right; X += Dash + Gap) {
        paint::line(Target, X, Y, std::min(X + Dash, Right), Y, static_cast<float>(px(1)), Color, 0.85f);
    }
}

std::string ago_text(std::size_t Ago) {
    return Ago == 0 ? std::string("刚才") : std::format("{} 秒前", Ago);
}

} // namespace

struct TrendChart::Legend {
    std::string Text;
    Rgb Color;
    bool Hidden = false;
    std::optional<std::size_t> Player; ///< 有值时点击可以隐藏或显示这名玩家的线
    float Left = 0.0f;
    float Right = 0.0f;
};

struct TrendChart::Plot {
    struct Series {
        const TimeSeries *Data = nullptr;
        std::string Name;
        Rgb Color;
    };

    std::vector<Series> Lines;
    std::vector<Series> Bars;
    std::vector<Legend> Legends;
    std::optional<double> Reference;
    std::string ReferenceLabel;
    Rgb ReferenceColor;
    double Floor = 1.0;
    std::function<std::string(double)> Format;
};

TrendChart::TrendChart(const ViewServices &Context, Node Parent, const Pack &Layout, Kind Shown)
    : Services(Context), Which(Shown), Legends(std::make_shared<std::vector<Legend>>()) {
    Area = Services.Ui.surface(
        Parent, Layout,
        [this](BuiRaster *Target, const Surface::Box &Box, const Surface &Self) { paint(Target, Box, Self); },
        [this](const Surface::Pointer &Where) { click(Where); });
}

Node TrendChart::node() const {
    return Area->node();
}

void TrendChart::refresh() const {
    Area->invalidate();
}

TrendChart::Plot TrendChart::plot(const Palette &Colors) const {
    const AppModel &Model = Services.Model;
    const StatsBoard &Board = Model.stats();
    Plot Result;
    switch (Which) {
    case Kind::Latency:
        for (std::size_t Player = 0; Player < Board.latency().size(); ++Player) {
            const Rgb Color = Colors.Series[Player % Colors.Series.size()];
            const bool Hidden = Model.player_hidden(Player);
            std::string Name = std::format("玩家 {}", Player);
            Result.Legends.push_back({Name, Color, Hidden, Player});
            if (!Hidden) {
                Result.Lines.push_back({&Board.latency()[Player], std::move(Name), Color});
            }
        }
        Result.Reference = static_cast<double>(SlowRequestLimit.count());
        Result.ReferenceLabel = std::format("{} ms 慢请求", SlowRequestLimit.count());
        Result.ReferenceColor = Colors.Danger;
        Result.Floor = 50.0;
        Result.Format = format_ms;
        return Result;
    case Kind::Rates:
        Result.Bars = {{&Board.event_rate(), "房间事件", Colors.AccentBright}, {&Board.log_rate(), "日志", Colors.Text3}};
        Result.Floor = 5.0;
        Result.Format = format_rate;
        break;
    case Kind::Backlog:
        Result.Lines = {{&Board.backlog(), "积压", Colors.AccentBright}};
        Result.Reference = static_cast<double>(Board.latest().PeakBacklog);
        Result.ReferenceLabel = std::format("峰值 {}", Board.latest().PeakBacklog);
        Result.ReferenceColor = Colors.Text3;
        Result.Floor = 4.0;
        Result.Format = [](double Value) { return std::format("{:.0f}", Value); };
        break;
    }
    for (const auto *Group : {&Result.Bars, &Result.Lines}) {
        for (const Plot::Series &Item : *Group) {
            Result.Legends.push_back({Item.Name, Item.Color});
        }
    }
    return Result;
}

void TrendChart::paint(BuiRaster *Target, const Surface::Box &Box, const Surface &Self) const {
    const Palette &Colors = Services.Ui.style().palette();
    const Plot Data = plot(Colors);
    const std::size_t Window = range_seconds(Services.Model.stats_range());
    const float Px1 = static_cast<float>(px(1));

    // 图例：圆点加名字；隐藏的玩家画成空心、文字变淡。
    const float LegendH = static_cast<float>(px(LegendHeight));
    const float Radius = static_cast<float>(px(Dot));
    float Cursor = Box.X;
    std::vector<Legend> Placed;
    for (Legend Item : Data.Legends) {
        const float CenterY = Box.Y + LegendH * 0.5f;
        if (Item.Hidden) {
            paint::ring(Target, Cursor + Radius, CenterY, Radius, Px1 * 1.5f, Colors.Text4);
        } else {
            paint::disc(Target, Cursor + Radius, CenterY, Radius, Item.Color);
        }
        const float TextX = Cursor + Radius * 2.0f + static_cast<float>(px(6));
        const float Width = paint::text_width(Item.Text);
        paint::text(Target, TextX, Box.Y, Width + static_cast<float>(px(4)), LegendH, Item.Text,
                    Item.Hidden ? Colors.Text4 : Colors.Text2);
        Item.Left = Cursor - Box.X;
        Item.Right = TextX + Width - Box.X;
        Placed.push_back(std::move(Item));
        Cursor = TextX + Width + static_cast<float>(px(20));
    }
    *Legends = std::move(Placed);

    const ChartArea Plot{Box.X + static_cast<float>(px(Gutter)), Box.Y + LegendH + static_cast<float>(px(10)),
                         Box.W - static_cast<float>(px(Gutter + 12)), Box.H - LegendH - static_cast<float>(px(18))};
    if (Plot.W <= 0.0f || Plot.H <= 0.0f) {
        return;
    }
    std::vector<const TimeSeries *> Shown;
    for (const auto *Group : {&Data.Bars, &Data.Lines}) {
        for (const Plot::Series &Item : *Group) {
            Shown.push_back(Item.Data);
        }
    }
    const double Ceiling = chart_ceiling(Shown, Window, Data.Floor, Data.Reference.value_or(0.0));

    // 只有零线和顶线两条淡线，刻度只标零和最大值。
    const float LabelW = static_cast<float>(px(Gutter - 14));
    const float LabelH = static_cast<float>(px(20));
    paint::text(Target, Box.X, Plot.Y - LabelH * 0.5f, LabelW, LabelH, Data.Format(Ceiling), Colors.Text3,
                paint::Align::Right);
    paint::text(Target, Box.X, Plot.Y + Plot.H - LabelH * 0.5f, LabelW, LabelH, "0", Colors.Text3, paint::Align::Right);
    paint::line(Target, Plot.X, Plot.Y + Plot.H, Plot.X + Plot.W, Plot.Y + Plot.H, Px1, Colors.Text4, 0.6f);
    paint::line(Target, Plot.X, Plot.Y, Plot.X + Plot.W, Plot.Y, Px1, Colors.Text4, 0.25f);

    if (Data.Reference && *Data.Reference > 0.0) {
        const float Y = chart_y(*Data.Reference, Ceiling, Plot);
        dashed_line(Target, Plot.X, Plot.X + Plot.W, Y, Data.ReferenceColor);
        const float Width = paint::text_width(Data.ReferenceLabel);
        paint::text(Target, Plot.X + Plot.W - Width, Y - static_cast<float>(px(20)), Width + Px1 * 2.0f,
                    static_cast<float>(px(18)), Data.ReferenceLabel, Data.ReferenceColor);
    }

    for (std::size_t Group = 0; Group < Data.Bars.size(); ++Group) {
        const Plot::Series &Item = Data.Bars[Group];
        for (const ChartBar &Bar : chart_bars(*Item.Data, Window, Ceiling, Plot, Group, Data.Bars.size())) {
            paint::fill(Target, Bar.X, Bar.Y, Bar.W, Bar.H, Item.Color, 0.85f);
        }
    }
    for (const Plot::Series &Item : Data.Lines) {
        for (const std::vector<ChartPoint> &Segment : chart_polylines(*Item.Data, Window, Ceiling, Plot)) {
            if (Segment.size() == 1) {
                paint::disc(Target, Segment[0].X, Segment[0].Y, Px1 * 2.5f, Item.Color);
            }
            for (std::size_t Index = 1; Index < Segment.size(); ++Index) {
                paint::line(Target, Segment[Index - 1].X, Segment[Index - 1].Y, Segment[Index].X, Segment[Index].Y,
                            Px1 * 2.0f, Item.Color);
            }
        }
    }

    // 悬停：一条竖线，旁边的小卡片列出那一秒的数值。
    const Surface::Pointer &Pointer = Self.pointer();
    const std::optional<std::size_t> Ago =
        Pointer.Hover ? chart_slot(Box.X + Pointer.X, Window, Plot) : std::optional<std::size_t>();
    if (!Ago) {
        return;
    }
    const float HoverX = chart_x(*Ago, Window, Plot);
    paint::line(Target, HoverX, Plot.Y, HoverX, Plot.Y + Plot.H, Px1, Colors.Text3, 0.5f);

    std::vector<std::pair<std::string, std::optional<Rgb>>> Rows = {{ago_text(*Ago), std::nullopt}};
    for (const auto *Group : {&Data.Lines, &Data.Bars}) {
        for (const Plot::Series &Item : *Group) {
            const std::optional<double> Value = Item.Data->ago(*Ago);
            Rows.emplace_back(std::format("{}  {}", Item.Name, Value ? Data.Format(*Value) : std::string("—")), Item.Color);
            if (Value && Group == &Data.Lines) {
                paint::disc(Target, HoverX, chart_y(*Value, Ceiling, Plot), Px1 * 3.5f, Item.Color);
            }
        }
    }
    const float Indent = Radius * 2.0f + static_cast<float>(px(8));
    float CardW = 0.0f;
    for (const auto &[Text, Color] : Rows) {
        CardW = std::max(CardW, paint::text_width(Text) + (Color ? Indent : 0.0f));
    }
    CardW += static_cast<float>(px(24));
    const float RowH = static_cast<float>(px(TipRow));
    const float CardH = RowH * static_cast<float>(Rows.size()) + static_cast<float>(px(12));
    float CardX = HoverX + static_cast<float>(px(12));
    if (CardX + CardW > Box.X + Box.W) {
        CardX = HoverX - static_cast<float>(px(12)) - CardW;
    }
    const float CardY = Plot.Y;
    paint::round_rect(Target, CardX - Px1, CardY - Px1, CardW + Px1 * 2.0f, CardH + Px1 * 2.0f,
                      static_cast<float>(px(7)), Colors.Edge);
    paint::round_rect(Target, CardX, CardY, CardW, CardH, static_cast<float>(px(6)), Colors.Popup);
    float RowY = CardY + static_cast<float>(px(6));
    for (const auto &[Text, Color] : Rows) {
        float TextX = CardX + static_cast<float>(px(12));
        if (Color) {
            paint::disc(Target, TextX + Radius, RowY + RowH * 0.5f, Radius, *Color);
            TextX += Indent;
        }
        paint::text(Target, TextX, RowY, CardX + CardW - TextX, RowH, Text, Color ? Colors.Text : Colors.Text3);
        RowY += RowH;
    }
}

void TrendChart::click(const Surface::Pointer &Where) const {
    if (Where.Y > static_cast<float>(px(LegendHeight))) {
        return;
    }
    for (const Legend &Item : *Legends) {
        if (Item.Player && Where.X >= Item.Left && Where.X <= Item.Right) {
            Services.Model.toggle_player(*Item.Player);
            return;
        }
    }
}

} // namespace scriptroom::gui
