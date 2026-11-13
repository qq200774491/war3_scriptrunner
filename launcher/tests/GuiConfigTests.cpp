// 图形界面的配置草稿、主题、命令行。

#include "CommandLine.h"
#include "ConfigDraft.h"
#include "FileSystem.h"
#include "Json.h"
#include "TestHarness.h"
#include "Text.h"
#include "Theme.h"

#include <fstream>

namespace {

using namespace scriptroom;
using namespace scriptroom::gui;
using namespace testing;

ConfigDraft sample_draft() {
    ConfigDraft Draft;
    Draft.path(PathField::Map) = "maps\\test.w3x";
    Draft.path(PathField::LocalEntry) = "client/main.lua";
    Draft.path(PathField::CloudScripts) = "cloud";
    Draft.path(PathField::Archives) = "archives";
    Draft.path(PathField::Editor) = "editor";
    Draft.path(PathField::SourceGame) = "source";
    Draft.path(PathField::Game) = "game";
    Draft.Seconds = "3600";
    Draft.Timeout = "90";
    return Draft;
}

const Registrar DraftResolvesRelativePaths("gui.draft.relative_paths_use_config_folder", [] {
    const TempDirectory Temp;
    const fs::path ConfigPath = Temp.path() / "sub" / "config.json";
    const Config Result = sample_draft().to_config(ConfigPath);
    check_equal(Result.Map.lexically_normal(), (Temp.path() / "sub" / "maps" / "test.w3x").lexically_normal());
    check_equal(Result.CloudScripts.lexically_normal(), (Temp.path() / "sub" / "cloud").lexically_normal());
    check_equal(Result.Seconds, std::uint64_t(3600));
    check_equal(Result.StartupTimeout, 90.0);
});

const Registrar DraftRoundTripsConfig("gui.draft.round_trip_keeps_absolute_paths", [] {
    const TempDirectory Temp;
    Config Source;
    Source.Map = Temp.path() / L"地图.w3x";
    Source.LocalEntry = Temp.path() / L"入口.lua";
    Source.CloudScripts = Temp.path() / L"云脚本";
    Source.Archives = Temp.path() / L"存档";
    Source.Editor = Temp.path() / L"editor";
    Source.SourceGame = Temp.path() / L"source";
    Source.Game = Temp.path() / L"game";
    Source.Seconds = 120;
    Source.StartupTimeout = 45.5;

    const ConfigDraft Draft = ConfigDraft::from_config(Source);
    check_equal(Draft.Seconds, std::string("120"));
    check_equal(Draft.Timeout, std::string("45.5"));
    check_equal(Draft.path(PathField::Map), path_to_utf8(Source.Map));

    const Config Back = Draft.to_config(Temp.path() / "config.json");
    check_equal(Back.Map, Source.Map);
    check_equal(Back.CloudScripts, Source.CloudScripts);
    check_equal(Back.Seconds, Source.Seconds);
    check_equal(Back.StartupTimeout, Source.StartupTimeout);
});

const Registrar DraftRejectsBadInput("gui.draft.rejects_bad_input", [] {
    const fs::path ConfigPath = fs::temp_directory_path() / "config.json";

    ConfigDraft Missing = sample_draft();
    Missing.path(PathField::Game) = "   ";
    check_throws([&] { Missing.to_config(ConfigPath); }, "请补全");

    ConfigDraft BadSeconds = sample_draft();
    BadSeconds.Seconds = "abc";
    check_throws([&] { BadSeconds.to_config(ConfigPath); }, "服务时长");
    BadSeconds.Seconds = "-5";
    check_throws([&] { BadSeconds.to_config(ConfigPath); }, "服务时长");

    ConfigDraft BadTimeout = sample_draft();
    BadTimeout.Timeout = "九十";
    check_throws([&] { BadTimeout.to_config(ConfigPath); }, "启动超时");

    ConfigDraft ZeroSeconds = sample_draft();
    ZeroSeconds.Seconds = "0";
    check_throws([&] { ZeroSeconds.to_config(ConfigPath); }, "运行时长");

    ConfigDraft HugeTimeout = sample_draft();
    HugeTimeout.Timeout = "99999";
    check_throws([&] { HugeTimeout.to_config(ConfigPath); }, "启动超时");

    ConfigDraft Padded = sample_draft();
    Padded.Seconds = "  120 ";
    Padded.path(PathField::Map) = "  maps/a.w3x  ";
    const Config Trimmed = Padded.to_config(ConfigPath);
    check_equal(Trimmed.Seconds, std::uint64_t(120));
    check_equal(Trimmed.Map.filename(), fs::path("a.w3x"));
});

const Registrar ThemePalettesMatchRust("gui.theme.palettes", [] {
    const Palette Blue = palette_of(ThemeKind::Shadowworks);
    check(Blue.Canvas == Rgb{32, 32, 32});
    check(Blue.Accent == Rgb{74, 139, 240});
    check(Blue.Selected == Rgb{35, 57, 86});
    check(Blue.Rail == Rgb{23, 64, 124});
    check(Blue.OnAccent == Rgb{255, 255, 255});

    const Palette Mono = palette_of(ThemeKind::Monochrome);
    check(Mono.Accent == Rgb{216, 216, 216});
    check(Mono.Selected == Rgb{60, 60, 60});
    check(Mono.Rail == Rgb{30, 30, 30});
    check(Mono.OnAccent == Rgb{0, 0, 0});

    const Palette Light = palette_of(ThemeKind::Light);
    check(Light.Canvas == Rgb{255, 255, 255});
    check(Light.Panel == Rgb{246, 247, 249});
    check(Light.Accent == Rgb{55, 112, 215});
    check(Light.Text == Rgb{45, 50, 58});
    check_equal(Light.Accent.hex(), std::string("#3770d7"));

    check_equal(std::string(theme_label(ThemeKind::Shadowworks)), std::string("深灰蓝"));
    check_equal(std::string(theme_label(ThemeKind::Monochrome)), std::string("黑白灰"));
    check_equal(std::string(theme_label(ThemeKind::Light)), std::string("浅色"));
    for (const ThemeKind Kind : AllThemes) {
        check(theme_from_key(theme_key(Kind)) == Kind);
    }
    check(!theme_from_key("pink").has_value());
});

const Registrar ThemeFileRoundTrip("gui.theme.settings_file", [] {
    const TempDirectory Temp;
    check(load_theme(Temp.path()) == ThemeKind::Shadowworks); // 没有文件：默认主题

    for (const ThemeKind Kind : AllThemes) {
        save_theme(Temp.path(), Kind);
        check(load_theme(Temp.path()) == Kind);
    }
    // 文件格式与 Rust 版兼容：{"theme": "light"}。
    save_theme(Temp.path(), ThemeKind::Light);
    const Json Body = parse_json(read_utf8_file(Temp.path() / L"界面设置.json"));
    check_equal(Body.at("theme").get<std::string>(), std::string("light"));

    atomic_write(Temp.path() / L"界面设置.json", "{\"theme\": \"nonsense\"}");
    check(load_theme(Temp.path()) == ThemeKind::Shadowworks);
    atomic_write(Temp.path() / L"界面设置.json", "not json");
    check(load_theme(Temp.path()) == ThemeKind::Shadowworks);
    atomic_write(Temp.path() / L"界面设置.json", "{\"theme\": \"monochrome\"}");
    check(load_theme(Temp.path()) == ThemeKind::Monochrome);
});

const Registrar ThemeJsonForBui("gui.theme.bui_json", [] {
    for (const ThemeKind Kind : AllThemes) {
        const Json File = parse_json(bui_theme_json(Kind));
        check_equal(File.at("themes").size(), std::size_t(1));
        const Json &Theme = File.at("themes").at(0);
        check_equal(Theme.at("name").get<std::string>(), bui_theme_name(Kind));
        check_equal(Theme.at("mode").get<std::string>(), std::string(Kind == ThemeKind::Light ? "light" : "dark"));
        const Palette Colors = palette_of(Kind);
        const Json &Table = Theme.at("colors");
        check_equal(Table.at("primary.background").get<std::string>(), Colors.Accent.hex());
        check_equal(Table.at("background").get<std::string>(), Colors.Canvas.hex());
        check_equal(Table.at("checkbox.text.selected").get<std::string>(), Colors.Text.hex());
        check_equal(Table.at("button.gradient.enabled").get<bool>(), false);
    }
    check(bui_theme_name(ThemeKind::Light) != bui_theme_name(ThemeKind::Monochrome));
});

const Registrar CommandLineDefaults("gui.command_line.defaults", [] {
    const LaunchOptions Plain = parse_launch_options({});
    check(!Plain.AutoStart);
    check(!Plain.Capture.has_value());
    check_equal(Plain.Width, DefaultWindowWidth);
    check_equal(Plain.Height, DefaultWindowHeight);

    check(parse_launch_options({"--start"}).AutoStart);
});

const Registrar CommandLineCapture("gui.command_line.capture", [] {
    const LaunchOptions Options = parse_launch_options({"--capture", "out.png", "--theme", "light", "--page", "archives",
                                                        "--width", "900", "--height", "580", "--capture-log", "a.log"});
    check(Options.Capture.has_value());
    check_equal(Options.Capture->Output, fs::path("out.png"));
    check(Options.Capture->Theme == ThemeKind::Light);
    check(Options.Capture->Start == Page::Archives);
    check(Options.Capture->LogFile.has_value());
    check_equal(Options.Width, 900);
    check_equal(Options.Height, 580);

    // 截图模式不启动游戏或服务，所以忽略 --start。
    check(!parse_launch_options({"--capture", "x.png", "--start"}).AutoStart);

    const LaunchOptions Fallback = parse_launch_options(
        {"--capture", "x.png", "--theme", "neon", "--page", "nowhere", "--width", "abc", "--height", "-3"});
    check(Fallback.Capture->Theme == ThemeKind::Shadowworks);
    check(Fallback.Capture->Start == Page::Console);
    check_equal(Fallback.Width, DefaultWindowWidth);
    check_equal(Fallback.Height, DefaultWindowHeight);

    check(parse_launch_options({"--theme", "light"}).Capture == std::nullopt);
    check(parse_launch_options({"--capture"}).Capture == std::nullopt); // 缺值：与 Rust 版一样视为没有该选项
    check(parse_launch_options({"--capture", "m.png", "--theme", "monochrome"}).Capture->Theme == ThemeKind::Monochrome);
});

} // namespace
