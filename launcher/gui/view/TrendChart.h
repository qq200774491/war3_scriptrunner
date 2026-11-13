#pragma once

#include "ViewServices.h"

#include <memory>

namespace scriptroom::gui {

/// 运行统计页的一张走势图：上面一行图例，下面是绘图区，鼠标停在图上显示那一秒的数值。
/// 数据和坐标都来自 AppModel 的统计面板与 StatsChart 里的换算，这里只负责画。
class TrendChart {
public:
    enum class Kind {
        Latency, ///< 每个玩家一条耗时线，加慢请求阈值
        Rates,   ///< 房间事件与日志的每秒柱状图
        Backlog, ///< 宿主队列积压曲线，加峰值线
    };

    TrendChart(const ViewServices &Context, Node Parent, const Pack &Layout, Kind Which);

    Node node() const;
    void refresh() const;

private:
    struct Legend;
    struct Plot;

    Plot plot(const Palette &Colors) const;
    void paint(BuiRaster *Target, const Surface::Box &Area, const Surface &Self) const;
    void click(const Surface::Pointer &Where) const;

    const ViewServices &Services;
    Kind Which;
    Surface *Area = nullptr;
    std::shared_ptr<std::vector<Legend>> Legends; ///< 上次绘制时各图例的位置，点击时据此判断
};

} // namespace scriptroom::gui
