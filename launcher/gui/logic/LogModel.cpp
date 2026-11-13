#include "LogModel.h"

#include "Text.h"

#include <format>

namespace scriptroom::gui {

namespace {

/// 云脚本范围只看是否来自云脚本，所以报错的脚本行同时属于“云脚本”与“错误与异常”两个范围。
bool in_entry_scope(LogScope Scope, const LogEntry &Entry) {
    switch (Scope) {
    case LogScope::Cloud:
        return Entry.Script;
    case LogScope::Errors:
        return Entry.Error;
    case LogScope::Slow:
        return Entry.Slow;
    case LogScope::All:
        break;
    }
    return true;
}

} // namespace

void LogModel::push_line(std::string_view Line) {
    LogEntry Entry;
    Entry.Text = std::string(Line);
    Entry.Folded = to_lower(Line);
    Entry.Script = is_script_line(Line);
    Entry.Slow = is_slow_line(Line);
    Entry.Error = !Entry.Slow && is_error_line(Line);
    Entry.Source = classify_line(Line);
    Entries.push_back(std::move(Entry));
    while (Entries.size() > Capacity) {
        Entries.pop_front();
    }
}

void LogModel::append(std::string_view Text) {
    bool Added = false;
    while (!Text.empty()) {
        const std::size_t Break = Text.find('\n');
        std::string_view Line = Text.substr(0, Break);
        Text = Break == std::string_view::npos ? std::string_view() : Text.substr(Break + 1);
        if (Line.ends_with('\r')) {
            Line.remove_suffix(1);
        }
        push_line(Line);
        Added = true;
    }
    if (Added) {
        ++Revision;
    }
}

void LogModel::clear() {
    Entries.clear();
    ++Revision;
}

std::size_t LogModel::count(LogScope Scope) const {
    std::size_t Total = 0;
    for (const LogEntry &Entry : Entries) {
        Total += in_entry_scope(Scope, Entry) ? 1 : 0;
    }
    return Total;
}

std::vector<std::size_t> LogModel::visible(const LogFilter &Filter) const {
    const std::string Needle = to_lower(Filter.Query);
    std::vector<std::size_t> Result;
    for (std::size_t Index = 0; Index < Entries.size(); ++Index) {
        const LogEntry &Entry = Entries[Index];
        const bool Matches = Needle.empty() || Entry.Folded.find(Needle) != std::string::npos;
        if (Matches && in_entry_scope(Filter.Scope, Entry)) {
            Result.push_back(Index);
        }
    }
    return Result;
}

std::string LogModel::copy_text(const LogFilter &Filter) const {
    std::string Result;
    for (const std::size_t Index : visible(Filter)) {
        const LogEntry &Entry = Entries[Index];
        if (!Result.empty()) {
            Result += "\r\n";
        }
        Result += std::format("[{}] {}", source_label(Entry.Source), Entry.Text);
    }
    return Result;
}

} // namespace scriptroom::gui
