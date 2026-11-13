#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace scriptroom::gui {

/// 固定长度的时间序列：约每秒一个点，写满后丢掉最旧的。某一秒没有数据（例如没有请求）时记为空。
class TimeSeries {
public:
    explicit TimeSeries(std::size_t Capacity) : Limit(Capacity) {}

    void push(std::optional<double> Value);
    void clear() { Points.clear(); }

    std::size_t capacity() const { return Limit; }
    std::size_t size() const { return Points.size(); }
    /// 往前数第 Ago 个点，0 是最新的；超出已有的点时为空。
    std::optional<double> ago(std::size_t Ago) const;
    /// 最近 Window 个点里非空点的最大值；没有时为空。
    std::optional<double> max(std::size_t Window) const;

private:
    std::deque<std::optional<double>> Points;
    std::size_t Limit;
};

/// 图表的绘图区，物理像素。
struct ChartArea {
    float X = 0.0f;
    float Y = 0.0f;
    float W = 0.0f;
    float H = 0.0f;
};

struct ChartPoint {
    float X = 0.0f;
    float Y = 0.0f;
};

struct ChartBar {
    float X = 0.0f;
    float Y = 0.0f;
    float W = 0.0f;
    float H = 0.0f;
};

// 下面的换算都只看最近 Window 秒：绘图区横向均分成 Window 格，最新的一秒在最右边一格。

/// 纵轴上限：各序列最近 Window 个点和参考值里的最大值，向上取到 1、2、5 乘 10 的整数次幂；都没有数据时取 Floor。
double chart_ceiling(std::span<const TimeSeries *const> Series, std::size_t Window, double Floor,
                     double Reference = 0.0);

/// 数值的纵坐标；超出量程的值贴在边上。
float chart_y(double Value, double Ceiling, const ChartArea &Area);

/// Ago 秒前那一格中心的横坐标。
float chart_x(std::size_t Ago, std::size_t Window, const ChartArea &Area);

/// 横坐标落在哪一格，返回那是几秒前；在绘图区外时为空。
std::optional<std::size_t> chart_slot(float X, std::size_t Window, const ChartArea &Area);

/// 折线的各段，从旧到新；空值把折线断开。只有一个点的段也保留，由视图画成圆点。
std::vector<std::vector<ChartPoint>> chart_polylines(const TimeSeries &Series, std::size_t Window, double Ceiling,
                                                     const ChartArea &Area);

/// 柱状图：每格分成 Groups 份，第 Group 份画这条序列；空值和零不出柱。
std::vector<ChartBar> chart_bars(const TimeSeries &Series, std::size_t Window, double Ceiling, const ChartArea &Area,
                                 std::size_t Group, std::size_t Groups);

} // namespace scriptroom::gui
