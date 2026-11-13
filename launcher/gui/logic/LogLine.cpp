#include "LogLine.h"

#include "Text.h"

#include <string>

namespace scriptroom::gui {

bool is_script_line(std::string_view Line) {
    return Line.find("[云脚本") != std::string_view::npos;
}

bool is_error_line(std::string_view Line) {
    if (Line.find("故障") != std::string_view::npos) {
        return true;
    }
    const std::string Lower = to_lower(Line);
    return Lower.find("error") != std::string::npos || Lower.find("failed") != std::string::npos;
}

LogSource classify_line(std::string_view Line) {
    if (is_error_line(Line)) {
        return LogSource::Error;
    }
    return is_script_line(Line) ? LogSource::Script : LogSource::System;
}

const char *source_label(LogSource Source) {
    switch (Source) {
    case LogSource::Error:
        return "错误";
    case LogSource::Script:
        return "脚本";
    case LogSource::System:
        break;
    }
    return "系统";
}

const char *scope_label(LogScope Scope) {
    switch (Scope) {
    case LogScope::Cloud:
        return "云脚本";
    case LogScope::Errors:
        return "错误与异常";
    case LogScope::All:
        break;
    }
    return "全部日志";
}

} // namespace scriptroom::gui
