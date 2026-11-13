// ScriptRoom 图形界面入口（Windows GUI 子系统）。
//
// 参数：--start 打开后自动启动一次；
//       --capture <png> [--theme blue|teal|violet|amber|slate|white] [--page settings|archives|about]
//                       [--width N --height M] [--capture-log <日志文件>]
//       截图模式不打开窗口，也不启动游戏或服务。

#include "Application.h"
#include "FileSystem.h"
#include "Fonts.h"
#include "SystemDialogs.h"
#include "Text.h"

#include <optional>
#include <string>
#include <vector>

#include <shellapi.h>
#include <windows.h>

namespace {

using namespace scriptroom;
using namespace scriptroom::gui;

std::vector<std::string> command_line_tokens() {
    int Count = 0;
    wchar_t **Wide = CommandLineToArgvW(GetCommandLineW(), &Count);
    std::vector<std::string> Tokens;
    for (int Index = 1; Wide != nullptr && Index < Count; ++Index) {
        Tokens.push_back(to_utf8(Wide[Index]));
    }
    LocalFree(Wide);
    return Tokens;
}

/// 截图模式下没有人看消息框：把错误写到 <png>.error.txt，调用方据此判断失败。
void record_capture_failure(const LaunchOptions &Options, const std::string &Message) {
    fs::path Path = Options.Capture->Output;
    try {
        atomic_write(Path.replace_extension(".error.txt"), Message);
    } catch (const std::exception &) {
        // 连错误文件都写不了时只能放弃；退出码仍然表明失败。
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::optional<LaunchOptions> Options;
    try {
        const ComScope Com;
        Options = parse_launch_options(command_line_tokens());
        use_system_cjk_font(); // 必须在创建任何 BUI 节点或窗口之前
        Application App(*Options);
        return App.run();
    } catch (const std::exception &Failure) {
        if (Options && Options->Capture) {
            record_capture_failure(*Options, Failure.what());
        } else {
            show_startup_failure(Failure.what());
        }
    } catch (...) {
        show_startup_failure("未知错误");
    }
    return 1;
}
