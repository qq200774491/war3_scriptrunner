#include "StatsChart.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace scriptroom::gui {

namespace {

double nice_ceiling(double Value) {
    const double Power = std::pow(10.0, std::floor(std::log10(Value)));
    for (const double Step : std::array<double, 3>{1.0, 2.0, 5.0}) {
        if (Value <= Step * Power) {
            return Step * Power;
        }
    }
    return 10.0 * Power;
}

float slot_width(std::size_t Window, const ChartArea &Area) {
    return Area.W / static_cast<float>(Window);
}

} // namespace

void TimeSeries::push(std::optional<double> Value) {
    Points.push_back(Value);
    while (Points.size() > Limit) {
        Points.pop_front();
    }
}

std::optional<double> TimeSeries::ago(std::size_t Ago) const {
    return Ago < Points.size() ? Points[Points.size() - 1 - Ago] : std::nullopt;
}

std::optional<double> TimeSeries::max(std::size_t Window) const {
    std::optional<double> Result;
    for (std::size_t Ago = 0; Ago < std::min(Window, Points.size()); ++Ago) {
        const std::optional<double> Point = ago(Ago);
        if (Point && (!Result || *Point > *Result)) {
            Result = Point;
        }
    }
    return Result;
}

double chart_ceiling(std::span<const TimeSeries *const> Series, std::size_t Window, double Floor, double Reference) {
    double Peak = Reference;
    for (const TimeSeries *Item : Series) {
        Peak = std::max(Peak, Item->max(Window).value_or(0.0));
    }
    return Peak > 0.0 ? nice_ceiling(Peak) : Floor;
}

float chart_y(double Value, double Ceiling, const ChartArea &Area) {
    const double Ratio = Ceiling > 0.0 ? std::clamp(Value / Ceiling, 0.0, 1.0) : 0.0;
    return Area.Y + Area.H * static_cast<float>(1.0 - Ratio);
}

float chart_x(std::size_t Ago, std::size_t Window, const ChartArea &Area) {
    const std::size_t Column = Window - 1 - std::min(Ago, Window - 1);
    return Area.X + slot_width(Window, Area) * (static_cast<float>(Column) + 0.5f);
}

std::optional<std::size_t> chart_slot(float X, std::size_t Window, const ChartArea &Area) {
    if (Window == 0 || X < Area.X || X >= Area.X + Area.W) {
        return std::nullopt;
    }
    const auto Column = static_cast<std::size_t>((X - Area.X) / slot_width(Window, Area));
    return Window - 1 - std::min(Column, Window - 1);
}

std::vector<std::vector<ChartPoint>> chart_polylines(const TimeSeries &Series, std::size_t Window, double Ceiling,
                                                     const ChartArea &Area) {
    std::vector<std::vector<ChartPoint>> Segments;
    bool Open = false;
    for (std::size_t Step = 0; Step < std::min(Window, Series.size()); ++Step) {
        const std::size_t Ago = std::min(Window, Series.size()) - 1 - Step;
        const std::optional<double> Value = Series.ago(Ago);
        if (!Value) {
            Open = false;
            continue;
        }
        if (!Open) {
            Segments.emplace_back();
            Open = true;
        }
        Segments.back().push_back({chart_x(Ago, Window, Area), chart_y(*Value, Ceiling, Area)});
    }
    return Segments;
}

std::vector<ChartBar> chart_bars(const TimeSeries &Series, std::size_t Window, double Ceiling, const ChartArea &Area,
                                 std::size_t Group, std::size_t Groups) {
    std::vector<ChartBar> Bars;
    if (Window == 0 || Groups == 0 || Group >= Groups) {
        return Bars;
    }
    const float Slot = slot_width(Window, Area);
    const float Gap = Slot * 0.2f;
    const float Width = (Slot - Gap) / static_cast<float>(Groups);
    const float Bottom = Area.Y + Area.H;
    for (std::size_t Ago = 0; Ago < std::min(Window, Series.size()); ++Ago) {
        const std::optional<double> Value = Series.ago(Ago);
        if (!Value || *Value <= 0.0) {
            continue;
        }
        const float Left = chart_x(Ago, Window, Area) - Slot / 2.0f + Gap / 2.0f + Width * static_cast<float>(Group);
        const float Top = chart_y(*Value, Ceiling, Area);
        Bars.push_back({Left, Top, Width, Bottom - Top});
    }
    return Bars;
}

} // namespace scriptroom::gui
