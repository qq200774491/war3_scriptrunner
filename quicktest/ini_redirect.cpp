#include "ini_redirect.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <stdexcept>

namespace quicktest {
namespace {
class File final {
public:
    explicit File(HANDLE value) : value_(value) {
        if (value == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open or lock player INI");
    }
    ~File() { if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    HANDLE release() noexcept { const auto value = value_; value_ = INVALID_HANDLE_VALUE; return value; }
private:
    HANDLE value_;
};

// 模块被固定至进程结束；缓冲区不分配内存，回调内没有异常和临时字符串生命周期。
struct Routing final {
    std::array<char, MAX_PATH> source{};
    std::array<char, MAX_PATH> destination{};
    std::array<wchar_t, MAX_PATH> audit{};
    std::atomic_flag read_seen = ATOMIC_FLAG_INIT;
    std::atomic_flag write_seen = ATOMIC_FLAG_INIT;
    HANDLE lock = INVALID_HANDLE_VALUE;
    bool installed = false;
    ~Routing() { if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock); }
} routing;

void to_ansi(const wchar_t* source, char* target, int size) {
    BOOL replaced = FALSE;
    const auto codepage = GetACP();
    const auto flags = codepage == CP_UTF8 ? 0 : WC_NO_BEST_FIT_CHARS;
    auto* used_default = codepage == CP_UTF8 ? nullptr : &replaced;
    if (!WideCharToMultiByte(codepage, flags, source, -1, target, size, nullptr, used_default) || replaced)
        throw std::runtime_error("INI path cannot be represented by the platform ANSI API");
}

void trace_once(std::atomic_flag& seen, const char* message) noexcept {
    if (seen.test_and_set() || !routing.audit[0]) return;
    const auto file = CreateFileW(routing.audit.data(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
    CloseHandle(file);
}

LPCSTR redirected_path(LPCSTR path, bool writing) noexcept {
    if (!path) return path;
    std::array<char, MAX_PATH> full{};
    const auto saved_error = GetLastError();
    const auto length = GetFullPathNameA(path, static_cast<DWORD>(full.size()), full.data(), nullptr);
    const bool matches = length && length < full.size() && _stricmp(full.data(), routing.source.data()) == 0;
    if (matches) {
        if (writing) trace_once(routing.write_seen, "INI_WRITE_ROUTED\n");
        else trace_once(routing.read_seen, "INI_READ_ROUTED\n");
    }
    SetLastError(saved_error);
    return matches ? routing.destination.data() : path;
}

DWORD WINAPI redirect_get_string(LPCSTR section, LPCSTR key, LPCSTR fallback,
                                 LPSTR output, DWORD size, LPCSTR path) noexcept {
    return GetPrivateProfileStringA(section, key, fallback, output, size, redirected_path(path, false));
}
UINT WINAPI redirect_get_int(LPCSTR section, LPCSTR key, INT fallback, LPCSTR path) noexcept {
    return GetPrivateProfileIntA(section, key, fallback, redirected_path(path, false));
}
BOOL WINAPI redirect_write_string(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR path) noexcept {
    return WritePrivateProfileStringA(section, key, value, redirected_path(path, true));
}

struct ImportHook {
    const char* name;
    void* replacement;
    void** slot = nullptr;
    void* original = nullptr;
    DWORD protection = 0;
};

void patch_imports(HMODULE module) {
    std::array<ImportHook, 3> hooks{{
        {"GetPrivateProfileStringA", reinterpret_cast<void*>(&redirect_get_string)},
        {"GetPrivateProfileIntA", reinterpret_cast<void*>(&redirect_get_int)},
        {"WritePrivateProfileStringA", reinterpret_cast<void*>(&redirect_write_string)},
    }};
    const auto base = reinterpret_cast<unsigned char*>(module);
    const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) throw std::runtime_error("platform has no imports");
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + rva);
    for (; imports->Name; ++imports) {
        if (!imports->OriginalFirstThunk) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const auto name = reinterpret_cast<const char*>(
                reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name);
            const bool profile_api = std::strncmp(name, "GetPrivateProfile", 17) == 0 ||
                                     std::strncmp(name, "WritePrivateProfile", 19) == 0;
            if (profile_api && std::none_of(hooks.begin(), hooks.end(),
                [name](const ImportHook& hook) { return std::strcmp(name, hook.name) == 0; }))
                throw std::runtime_error("unsupported platform profile API");
            for (auto& hook : hooks) {
                if (std::strcmp(hook.name, name) != 0) continue;
                if (hook.slot) throw std::runtime_error("duplicate platform profile import");
                hook.slot = reinterpret_cast<void**>(&slots->u1.Function);
                hook.original = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), hook.name));
                if (*hook.slot != hook.original) throw std::runtime_error("platform profile import already modified");
            }
        }
    }
    // 修改前一次性验证完整入口集合，未知版本不退回共用文件。
    for (const auto& hook : hooks) {
        if (!hook.slot) throw std::runtime_error("platform profile API set changed");
    }
    std::size_t applied = 0;
    bool complete = true;
    for (auto& hook : hooks) {
        if (!VirtualProtect(hook.slot, sizeof(void*), PAGE_READWRITE, &hook.protection)) { complete = false; break; }
        InterlockedExchangePointer(hook.slot, hook.replacement);
        ++applied;
        DWORD ignored = 0;
        if (!VirtualProtect(hook.slot, sizeof(void*), hook.protection, &ignored)) { complete = false; break; }
    }
    if (!complete) {
        for (std::size_t i = 0; i < applied; ++i) {
            DWORD ignored = 0;
            if (!VirtualProtect(hooks[i].slot, sizeof(void*), PAGE_READWRITE, &ignored)) {
                TerminateProcess(GetCurrentProcess(), 1);
                ExitProcess(1);
            }
            InterlockedExchangePointer(hooks[i].slot, hooks[i].original);
            VirtualProtect(hooks[i].slot, sizeof(void*), hooks[i].protection, &ignored);
        }
        throw std::runtime_error("cannot redirect platform INI imports");
    }
}
}

void install_ini_redirect(HMODULE platform, const std::wstring& destination, const std::wstring& audit_path) {
    if (!platform || routing.installed) throw std::runtime_error("invalid INI redirect installation");
    std::array<wchar_t, MAX_PATH> executable{};
    auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) throw std::runtime_error("game executable path too long");
    const auto slash = std::wcsrchr(executable.data(), L'\\');
    if (!slash) throw std::runtime_error("game executable path has no directory");
    const std::wstring source(executable.data(), static_cast<std::size_t>(slash - executable.data() + 1));
    std::array<wchar_t, MAX_PATH> target{};
    length = GetFullPathNameW(destination.c_str(), static_cast<DWORD>(target.size()), target.data(), nullptr);
    if (!length || length >= target.size()) throw std::runtime_error("player INI path too long");
    to_ansi((source + L"dz_w3_plugin.ini").c_str(), routing.source.data(), static_cast<int>(routing.source.size()));
    to_ansi(target.data(), routing.destination.data(), static_cast<int>(routing.destination.size()));
    if (_stricmp(routing.source.data(), routing.destination.data()) == 0)
        throw std::runtime_error("player INI must not be the shared game INI");
    if (audit_path.size() >= routing.audit.size()) throw std::runtime_error("INI audit path too long");
    std::copy(audit_path.begin(), audit_path.end(), routing.audit.begin());
    const std::wstring lock_path = std::wstring(target.data()) + L".lock";
    File lock(CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    File ini(CreateFileW(target.data(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    // 不允许其他代码卸载含回调的 DLL；进程退出统一回收导入表、路径和文件锁。
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&redirect_get_string), &self))
        throw std::runtime_error("cannot retain INI redirect module");
    patch_imports(platform);
    routing.lock = lock.release();
    routing.installed = true;
}
}
