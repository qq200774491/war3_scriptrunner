#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <windows.h>

namespace scriptroom::gui {

namespace fs = std::filesystem;

/// 系统文件对话框的选项；均为 UTF-8。
struct FileDialogOptions {
    std::string Title;
    std::string InitialName;      ///< 另存为时预填的文件名
    std::string FilterLabel;      ///< 例如 "JSON"；为空表示不过滤
    std::string FilterExtension;  ///< 例如 "json"
    fs::path StartFolder;         ///< 对话框打开时所在目录；不存在则忽略
};

/// 以下三个对话框都是模态的，调用期间会在界面线程里嵌套消息循环。
/// 取消返回空；系统调用失败时抛出 Error。线程必须已初始化 COM（单线程套间）。
std::optional<fs::path> pick_file(HWND Owner, const FileDialogOptions &Options);
std::optional<fs::path> pick_folder(HWND Owner, const FileDialogOptions &Options);
std::optional<fs::path> pick_save_file(HWND Owner, const FileDialogOptions &Options);

/// 用资源管理器打开目录；失败时抛出 Error。
void open_in_explorer(const fs::path &Directory);

/// 启动失败的系统消息框（此时界面库尚未可用）。
void show_startup_failure(std::string_view Message);

/// 一个 COM 单线程套间的初始化范围。
class ComScope {
public:
    ComScope();
    ~ComScope();
    ComScope(const ComScope &) = delete;
    ComScope &operator=(const ComScope &) = delete;

private:
    bool Initialized = false;
};

} // namespace scriptroom::gui
