#pragma once

#include "Segmented.h"
#include "TrendChart.h"
#include "ViewServices.h"

#include <array>
#include <cstdint>
#include <memory>

namespace scriptroom::gui {

/// 运行统计页：一行关键数字、每个玩家一行的明细表、响应耗时走势，以及并排的速率图和积压图。
/// 服务还没启动过时只显示一句提示。
class StatsPage {
public:
    StatsPage(const ViewServices &Context, Node Parent);

    Node root() const { return Body; }
    void sync();
    /// 主区域的宽度（物理像素）：太窄时下面两张图改成上下叠放。
    void resize(int Width);

private:
    struct Metric {
        Node Caption;
        Node Value;
    };

    void build_toolbar(Node Toolbar);
    void build_metrics(Node Parent);
    void layout_bottom(bool Stacked);
    void paint_table(BuiRaster *Target, const Surface::Box &Area) const;
    std::uint64_t data_stamp() const;

    const ViewServices &Services;
    Node Body;
    Node Empty;
    Node Content;
    std::array<Metric, 4> Metrics;
    Surface *Table = nullptr;
    Segmented Ranges;
    PillButton Pause;
    std::unique_ptr<TrendChart> Latency;
    Node Bottom;
    std::array<Node, 2> BottomCells;
    std::unique_ptr<TrendChart> Rates;
    std::unique_ptr<TrendChart> Backlog;
    std::size_t TableRows = 0;
    bool Stacked = false;
    std::uint64_t Stamp = 0;
};

} // namespace scriptroom::gui
