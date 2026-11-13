#include "session.hpp"
#include <algorithm>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace quicktest {
namespace {
std::string ascii(const std::wstring& text, std::size_t maximum) {
    if (text.empty() || text.size() > maximum) throw std::runtime_error("invalid argument length");
    std::string result;
    for (wchar_t ch : text) {
        if (!((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'z') ||
              (ch >= L'A' && ch <= L'Z') || ch == L'-' || ch == L'_'))
            throw std::runtime_error("room/player must use ASCII letters, digits, hyphen or underscore");
        result.push_back(static_cast<char>(ch));
    }
    return result;
}
std::string ansi_path(const std::wstring& text) {
    BOOL replaced = FALSE;
    const auto count = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, &replaced);
    std::string result(count, '\0');
    if (!count || !WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), count, nullptr, &replaced) || replaced)
        throw std::runtime_error("map path cannot be represented in game code page");
    return result;
}
Options parse_options() {
    int count = 0;
    auto* raw = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!raw) throw std::runtime_error("cannot read command line");
    std::unique_ptr<void, decltype(&LocalFree)> owner(raw, &LocalFree);
    std::vector<std::wstring> args(raw, raw + count);
    Options result;
    int roles = 0;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto key = args[i];
        auto value = [&]() -> const std::wstring& {
            if (++i == args.size()) throw std::runtime_error("missing quicktest argument");
            return args[i];
        };
        if (key == L"-host") {
            ++roles;
            result.host = true;
            // 启动器一次开 1 到 4 个客户端；房主等到齐这么多人才开局，只有自己时建好房间就开。
            const auto& players = value();
            if (players.size() != 1 || players[0] < L'1' || players[0] > L'4')
                throw std::runtime_error("host player count must be 1 to 4");
            result.players = players[0] - L'0';
            auto path = std::filesystem::path(value());
            if (path.has_root_path() || path.empty() || *path.begin() != L"Maps" ||
                std::find(path.begin(), path.end(), L"..") != path.end() ||
                !std::filesystem::is_regular_file(path))
                throw std::runtime_error("host map must exist under the game's relative Maps directory");
            result.map = ansi_path(path.make_preferred().wstring());
            if (result.map.size() >= 80) throw std::runtime_error("host map relative path is too long");
        } else if (key == L"-join") {
            ++roles;
        } else if (key == L"--qt-room") {
            result.room = value();
            ascii(result.room, 36);
        } else if (key == L"--qt-player") {
            result.player = ascii(value(), 15);
        } else if (key == L"--qt-log") {
            result.log_path = value();
        } else if (key == L"--qt-ini") {
            result.ini_path = value();
            if (!std::filesystem::path(result.ini_path).is_absolute())
                throw std::runtime_error("player INI path must be absolute");
        }
    }
    if (roles != 1 || result.room.empty() || result.player.empty() || result.log_path.empty())
        throw std::runtime_error("incomplete quicktest options");
    return result;
}
}
Session::~Session() {
    close_events();
    if (started_) CloseHandle(started_);
}
void Session::configure() {
    options_ = parse_options();
    const auto prefix = L"Local\\SharedScriptQuicktest-" + options_.room;
    ready_ = CreateEventW(nullptr, TRUE, FALSE, (prefix + L"-ready").c_str());
    const auto existed = GetLastError() == ERROR_ALREADY_EXISTS;
    failed_ = CreateEventW(nullptr, TRUE, FALSE, (prefix + L"-failed").c_str());
    if (options_.host) started_ = CreateEventW(nullptr, TRUE, FALSE, (prefix + L"-started").c_str());
    if (!ready_ || !failed_ || (options_.host && (!started_ || existed)))
        throw std::runtime_error("cannot create unique quicktest session events");
    log("CONFIGURED player=" + options_.player);
}
void Session::close_events() {
    if (ready_) CloseHandle(ready_);
    if (failed_) CloseHandle(failed_);
    ready_ = failed_ = nullptr;
}
void Session::ready() {
    if (ready_announced_) return;
    if (!SetEvent(ready_)) fail("cannot signal host ready");
    ready_announced_ = true;
    log("HOST_READY");
}
void Session::game_started() {
    // 独立开局事件保留至主机进程退出；关闭建房事件不会抹掉启动状态。
    if (!SetEvent(started_)) fail("cannot signal game start");
}
void Session::check_ready() const {
    if (WaitForSingleObject(failed_, 0) != WAIT_TIMEOUT || WaitForSingleObject(ready_, 0) != WAIT_OBJECT_0)
        throw std::runtime_error("host not ready; launcher must wait before starting join client");
}
void Session::log(const std::string& message) const {
    if (options_.log_path.empty()) { OutputDebugStringA(message.c_str()); return; }
    std::ofstream output(std::filesystem::path(options_.log_path), std::ios::app);
    output << GetTickCount64() << " pid=" << GetCurrentProcessId() << ' ' << message << '\n';
}
void Session::report_failure(std::string_view message) const noexcept {
    // 日志分配或 I/O 失败也不能让异常越过游戏回调边界。
    try { log("FAILED " + std::string(message)); }
    catch (...) { OutputDebugStringA("quicktest failure log unavailable\n"); }
    if (failed_) SetEvent(failed_);
}
[[noreturn]] void Session::fail(std::string_view message) const noexcept {
    report_failure(message);
    TerminateProcess(GetCurrentProcess(), 1);
    ExitProcess(1);
}
}
