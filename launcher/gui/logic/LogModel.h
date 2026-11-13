#pragma once

#include "LogLine.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace scriptroom::gui {

/// 一行日志及其分类结果；分类和小写折叠在写入时完成，筛选时不再重复计算。
struct LogEntry {
    std::string Text;
    std::string Folded; ///< to_lower(Text)，供不分大小写搜索
    LogSource Source = LogSource::System;
    bool Script = false;
    bool Error = false;
};

struct LogFilter {
    LogScope Scope = LogScope::All;
    std::string Query;

    bool operator==(const LogFilter &) const = default;
};

/// 控制台日志：只保留最近 Capacity 行。
/// 行号是保留窗口内的序号（旧行被丢弃后行号随之前移），与 Rust 版一致。
class LogModel {
public:
    static constexpr std::size_t Capacity = 5000;

    /// 按行拆分后追加；\r\n 与 \n 都视为换行，末尾换行不产生空行，空文本不产生任何行。
    void append(std::string_view Text);

    void clear();

    std::size_t size() const { return Entries.size(); }
    bool empty() const { return Entries.empty(); }
    const LogEntry &at(std::size_t Index) const { return Entries[Index]; }

    /// 每次内容变化（追加、丢弃、清空）加一，视图据此判断是否需要刷新。
    std::uint64_t revision() const { return Revision; }

    /// 范围内的行数（不受搜索词影响）。
    std::size_t count(LogScope Scope) const;

    /// 同时满足范围与搜索词的行序号，升序。搜索词为空表示不限。
    std::vector<std::size_t> visible(const LogFilter &Filter) const;

private:
    void push_line(std::string_view Line);

    std::deque<LogEntry> Entries;
    std::uint64_t Revision = 0;
};

} // namespace scriptroom::gui
