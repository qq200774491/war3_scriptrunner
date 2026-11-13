#include "Win32.h"

#include "Text.h"

#include <format>

namespace scriptroom {

std::string system_error_text(unsigned long Code) {
    wchar_t *Buffer = nullptr;
    const DWORD Length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, Code, 0, reinterpret_cast<LPWSTR>(&Buffer), 0,
                                        nullptr);
    std::string Description;
    if (Length > 0 && Buffer != nullptr) {
        std::wstring Text(Buffer, Length);
        while (!Text.empty() && (Text.back() == L'\r' || Text.back() == L'\n' || Text.back() == L' ')) {
            Text.pop_back();
        }
        Description = utf8_lossy(to_utf8(Text));
    }
    if (Buffer != nullptr) {
        LocalFree(Buffer);
    }
    if (Description.empty()) {
        return std::format("系统错误 {}", Code);
    }
    return std::format("{} (os error {})", Description, Code);
}

std::string last_error_text() {
    return system_error_text(GetLastError());
}

} // namespace scriptroom
