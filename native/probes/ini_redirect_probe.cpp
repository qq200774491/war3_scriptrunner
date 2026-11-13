#include "ini_redirect.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
class Library final {
public:
    explicit Library(const wchar_t* path) : value_(LoadLibraryW(path)) {
        if (!value_) throw std::runtime_error("cannot load fixture");
    }
    ~Library() { FreeLibrary(value_); }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    HMODULE get() const { return value_; }
private:
    HMODULE value_;
};
std::string ansi(const wchar_t* text) {
    const auto size = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("invalid test argument");
    std::string result(static_cast<std::size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_ACP, 0, text, -1, result.data(), size, nullptr, nullptr))
        throw std::runtime_error("test argument conversion failed");
    result.pop_back();
    return result;
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 6) throw std::runtime_error("fixture target requested key value [hold]");
        Library library(argv[1]);
        quicktest::install_ini_redirect(library.get(), argv[2], std::wstring(argv[2]) + L".audit");
        using Get = DWORD(__cdecl*)(LPCSTR, LPCSTR, LPCSTR, LPSTR, DWORD, LPCSTR);
        using Int = UINT(__cdecl*)(LPCSTR, LPCSTR, INT, LPCSTR);
        using Write = BOOL(__cdecl*)(LPCSTR, LPCSTR, LPCSTR, LPCSTR);
        const auto get = reinterpret_cast<Get>(GetProcAddress(library.get(), "fixture_get"));
        const auto integer = reinterpret_cast<Int>(GetProcAddress(library.get(), "fixture_int"));
        const auto write = reinterpret_cast<Write>(GetProcAddress(library.get(), "fixture_write"));
        if (!get || !integer || !write) throw std::runtime_error("missing fixture exports");
        const auto path = ansi(argv[3]);
        const auto key = ansi(argv[4]);
        const auto value = ansi(argv[5]);
        std::array<char, 256> buffer{};
        get("DzAPI", key.c_str(), "missing", buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
        std::cout << "BEFORE " << buffer.data() << '\n';
        std::cout << "INT " << integer("DzAPI", "level", 7, path.c_str()) << '\n';
        if (value != "--read-only") {
            if (!write("DzAPI", key.c_str(), value == "--delete" ? nullptr : value.c_str(), path.c_str()))
                throw std::runtime_error("profile write failed");
        }
        get("DzAPI", key.c_str(), "missing", buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
        std::cout << "AFTER " << buffer.data() << '\n';
        // 同一进程中的非平台模块导入不受影响。
        GetPrivateProfileStringA("DzAPI", key.c_str(), "missing", buffer.data(),
            static_cast<DWORD>(buffer.size()), path.c_str());
        std::cout << "OUTSIDE " << buffer.data() << '\n';
        std::cout << "READY\n" << std::flush;
        if (argc == 7) std::cin.get();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
