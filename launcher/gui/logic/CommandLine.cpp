#include "CommandLine.h"

#include "Text.h"

#include <algorithm>

namespace scriptroom::gui {

namespace {

/// 取 `--name value` 的 value；与 Rust 版相同，出现多次时取第一个。
std::optional<std::string> option_value(const std::vector<std::string> &Tokens, std::string_view Name) {
    for (std::size_t Index = 0; Index + 1 < Tokens.size(); ++Index) {
        if (Tokens[Index] == Name) {
            return Tokens[Index + 1];
        }
    }
    return std::nullopt;
}

int dimension(const std::vector<std::string> &Tokens, std::string_view Name, int Fallback) {
    const auto Text = option_value(Tokens, Name);
    const auto Value = Text ? parse_i64(*Text) : std::nullopt;
    return Value && *Value > 0 && *Value <= 16384 ? static_cast<int>(*Value) : Fallback;
}

ThemeKind capture_theme(const std::optional<std::string> &Name) {
    const std::optional<ThemeKind> Known = Name ? theme_from_key(*Name) : std::nullopt;
    return Known.value_or(ThemeKind::Blue);
}

Page capture_page(const std::optional<std::string> &Name) {
    for (const Page Candidate : {Page::Stats, Page::Settings, Page::Archives, Page::About}) {
        if (Name == page_key(Candidate)) {
            return Candidate;
        }
    }
    return Page::Console;
}

} // namespace

LaunchOptions parse_launch_options(const std::vector<std::string> &Tokens) {
    LaunchOptions Options;
    Options.Width = dimension(Tokens, "--width", DefaultWindowWidth);
    Options.Height = dimension(Tokens, "--height", DefaultWindowHeight);
    if (const auto Output = option_value(Tokens, "--capture")) {
        CaptureRequest Request;
        Request.Output = path_from_utf8(*Output);
        Request.Theme = capture_theme(option_value(Tokens, "--theme"));
        Request.Start = capture_page(option_value(Tokens, "--page"));
        if (const auto Log = option_value(Tokens, "--capture-log")) {
            Request.LogFile = path_from_utf8(*Log);
        }
        Request.SampleStats = std::find(Tokens.begin(), Tokens.end(), "--sample-stats") != Tokens.end();
        Options.Capture = std::move(Request);
        return Options;
    }
    Options.AutoStart = std::find(Tokens.begin(), Tokens.end(), "--start") != Tokens.end();
    return Options;
}

} // namespace scriptroom::gui
