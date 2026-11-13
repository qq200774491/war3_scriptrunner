#include "SystemDialogs.h"

#include "Error.h"
#include "Text.h"

#include <format>

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace scriptroom::gui {

namespace {

/// 最小的 COM 智能指针：只覆盖本文件需要的 取得 / 释放。
template <typename T> class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() {
        if (Pointer != nullptr) {
            Pointer->Release();
        }
    }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;

    T *operator->() const { return Pointer; }
    T *get() const { return Pointer; }
    /// 供 CoCreateInstance 等 API 写入；调用前必须为空。
    T **put() { return &Pointer; }

private:
    T *Pointer = nullptr;
};

void check_hresult(HRESULT Result, const char *Action) {
    require(SUCCEEDED(Result), std::format("{}失败（0x{:08X}）", Action, static_cast<unsigned long>(Result)));
}

void set_start_folder(IFileDialog &Dialog, const fs::path &Folder) {
    std::error_code Ignored;
    if (Folder.empty() || !fs::is_directory(Folder, Ignored)) {
        return;
    }
    ComPtr<IShellItem> Item;
    if (SUCCEEDED(SHCreateItemFromParsingName(Folder.c_str(), nullptr, IID_PPV_ARGS(Item.put())))) {
        Dialog.SetFolder(Item.get());
    }
}

/// 创建并配置对话框，显示后取回选择结果。Flags 追加到系统默认选项上。
std::optional<fs::path> run_dialog(const CLSID &Kind, HWND Owner, const FileDialogOptions &Options, FILEOPENDIALOGOPTIONS Flags) {
    ComPtr<IFileDialog> Dialog;
    check_hresult(CoCreateInstance(Kind, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(Dialog.put())), "创建文件对话框");

    FILEOPENDIALOGOPTIONS Current = 0;
    Dialog->GetOptions(&Current);
    Dialog->SetOptions(Current | Flags | FOS_FORCEFILESYSTEM);
    if (!Options.Title.empty()) {
        Dialog->SetTitle(to_wide(Options.Title).c_str());
    }
    if (!Options.InitialName.empty()) {
        Dialog->SetFileName(to_wide(Options.InitialName).c_str());
    }
    // 过滤器的两个字符串必须在 Show 返回前保持有效。
    const std::wstring Label = to_wide(Options.FilterLabel);
    const std::wstring Pattern = L"*." + to_wide(Options.FilterExtension);
    if (!Options.FilterLabel.empty() && !Options.FilterExtension.empty()) {
        const COMDLG_FILTERSPEC Spec = {Label.c_str(), Pattern.c_str()};
        Dialog->SetFileTypes(1, &Spec);
        Dialog->SetDefaultExtension(to_wide(Options.FilterExtension).c_str());
    }
    set_start_folder(*Dialog.get(), Options.StartFolder);

    const HRESULT Shown = Dialog->Show(Owner);
    if (Shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return std::nullopt;
    }
    check_hresult(Shown, "显示文件对话框");

    ComPtr<IShellItem> Result;
    check_hresult(Dialog->GetResult(Result.put()), "读取选择结果");
    PWSTR Name = nullptr;
    check_hresult(Result->GetDisplayName(SIGDN_FILESYSPATH, &Name), "读取所选路径");
    const fs::path Chosen(Name);
    CoTaskMemFree(Name);
    return Chosen;
}

} // namespace

std::optional<fs::path> pick_file(HWND Owner, const FileDialogOptions &Options) {
    return run_dialog(CLSID_FileOpenDialog, Owner, Options, FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
}

std::optional<fs::path> pick_folder(HWND Owner, const FileDialogOptions &Options) {
    return run_dialog(CLSID_FileOpenDialog, Owner, Options, FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
}

std::optional<fs::path> pick_save_file(HWND Owner, const FileDialogOptions &Options) {
    return run_dialog(CLSID_FileSaveDialog, Owner, Options, FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST);
}

void open_in_explorer(const fs::path &Directory) {
    const std::wstring Argument = L"\"" + Directory.wstring() + L"\"";
    const auto Result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", L"explorer.exe", Argument.c_str(), nullptr, SW_SHOWNORMAL));
    require(Result > 32, std::format("无法打开资源管理器（错误码 {}）", Result));
}

void show_startup_failure(std::string_view Message) {
    MessageBoxW(nullptr, to_wide(Message).c_str(), L"ScriptRoom 启动失败", MB_OK | MB_ICONERROR);
}

ComScope::ComScope() {
    // 套间已被别的模式初始化（RPC_E_CHANGED_MODE）时不要求配对释放。
    Initialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
}

ComScope::~ComScope() {
    if (Initialized) {
        CoUninitialize();
    }
}

} // namespace scriptroom::gui
