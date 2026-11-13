// 图形界面的日志逻辑：行分类、范围与搜索筛选、容量上限、行号。

#include "LogModel.h"
#include "TestHarness.h"

#include <format>

namespace {

using namespace scriptroom::gui;
using namespace testing;

const Registrar LogLineClassification("gui.log.classify_lines", [] {
    check(classify_line("[云脚本 0] alpha") == LogSource::Script);
    check(classify_line("[系统] beta") == LogSource::System);
    check(classify_line("plain text") == LogSource::System);
    // 错误优先于脚本：同一行既是云脚本输出又含 error 时，来源列显示“错误”。
    check(classify_line("[云脚本 error] boom") == LogSource::Error);
    for (const char *Line : {"error gamma", "ERROR: x", "Connection FAILED", "Failed to open", "故障：房间意外关闭"}) {
        check(classify_line(Line) == LogSource::Error, Line);
    }
    check(is_script_line("[云脚本] D:\\scripts"));
    check(!is_script_line("[房间] room"));
    check_equal(std::string(source_label(LogSource::System)), std::string("系统"));
    check_equal(std::string(source_label(LogSource::Script)), std::string("脚本"));
    check_equal(std::string(source_label(LogSource::Error)), std::string("错误"));
    check_equal(std::string(scope_label(LogScope::All)), std::string("全部日志"));
    check_equal(std::string(scope_label(LogScope::Cloud)), std::string("云脚本"));
    check_equal(std::string(scope_label(LogScope::Errors)), std::string("错误与异常"));
});

const Registrar LogModelSplitsLines("gui.log.append_splits_lines", [] {
    LogModel Model;
    Model.append("a\r\nb\n\nc\n");
    check_equal(Model.size(), std::size_t(4));
    check_equal(Model.at(0).Text, std::string("a"));
    check_equal(Model.at(1).Text, std::string("b"));
    check_equal(Model.at(2).Text, std::string(""));
    check_equal(Model.at(3).Text, std::string("c"));

    const std::uint64_t Before = Model.revision();
    Model.append(""); // 空文本不产生任何行，也不算变化
    check_equal(Model.revision(), Before);
    Model.append("tail");
    check(Model.revision() != Before);
    check_equal(Model.size(), std::size_t(5));
});

const Registrar LogModelKeepsLastLines("gui.log.capacity_drops_oldest", [] {
    LogModel Model;
    for (int Index = 0; Index < 5005; ++Index) {
        Model.append(std::format("line {}", Index));
    }
    check_equal(Model.size(), LogModel::Capacity);
    check_equal(Model.at(0).Text, std::string("line 5"));
    check_equal(Model.at(Model.size() - 1).Text, std::string("line 5004"));

    // 行号按保留窗口计：筛选结果里存的是窗口内下标。
    LogFilter Filter;
    Filter.Query = "line 5004";
    const std::vector<std::size_t> Rows = Model.visible(Filter);
    check_equal(Rows.size(), std::size_t(1));
    check_equal(Rows[0], std::size_t(4999));
});

const Registrar LogModelFilters("gui.log.filter_scope_and_query", [] {
    LogModel Model;
    Model.append("[云脚本 0] alpha\n[系统] beta\nerror gamma\n[云脚本 error] Delta\nÉCLAIR 错误");
    check_equal(Model.count(LogScope::All), std::size_t(5));
    check_equal(Model.count(LogScope::Cloud), std::size_t(2));
    check_equal(Model.count(LogScope::Errors), std::size_t(2));

    LogFilter Filter;
    Filter.Scope = LogScope::Cloud;
    check(Model.visible(Filter) == std::vector<std::size_t>({0, 3}));
    Filter.Scope = LogScope::Errors;
    check(Model.visible(Filter) == std::vector<std::size_t>({2, 3}));

    // 搜索不分大小写，非 ASCII 字符同样折叠。
    Filter.Scope = LogScope::All;
    Filter.Query = "ALPHA";
    check(Model.visible(Filter) == std::vector<std::size_t>({0}));
    Filter.Query = "éclair";
    check(Model.visible(Filter) == std::vector<std::size_t>({4}));
    Filter.Query = "delta";
    Filter.Scope = LogScope::Errors;
    check(Model.visible(Filter) == std::vector<std::size_t>({3}));
    Filter.Query = "不存在";
    check(Model.visible(Filter).empty());
});

const Registrar LogModelClear("gui.log.clear_bumps_revision", [] {
    LogModel Model;
    Model.append("one");
    const std::uint64_t Before = Model.revision();
    Model.clear();
    check(Model.empty());
    check(Model.revision() != Before);
});

} // namespace
