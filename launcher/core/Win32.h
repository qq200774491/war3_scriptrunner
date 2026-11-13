#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <utility>

namespace scriptroom {

/// 独占 Windows 内核句柄；析构时关闭。有效值既排除 nullptr 也排除 INVALID_HANDLE_VALUE。
class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE Raw) : Value(normalize(Raw)) {}
    UniqueHandle(const UniqueHandle &) = delete;
    UniqueHandle &operator=(const UniqueHandle &) = delete;
    UniqueHandle(UniqueHandle &&Other) noexcept : Value(Other.release()) {}
    UniqueHandle &operator=(UniqueHandle &&Other) noexcept {
        if (this != &Other) {
            reset(Other.release());
        }
        return *this;
    }
    ~UniqueHandle() { reset(); }

    HANDLE get() const { return Value; }
    bool valid() const { return Value != nullptr; }
    explicit operator bool() const { return valid(); }

    HANDLE release() { return std::exchange(Value, nullptr); }

    void reset(HANDLE Raw = nullptr) {
        if (Value != nullptr) {
            CloseHandle(Value);
        }
        Value = normalize(Raw);
    }

private:
    static HANDLE normalize(HANDLE Raw) { return Raw == INVALID_HANDLE_VALUE ? nullptr : Raw; }

    HANDLE Value = nullptr;
};

/// 把 Win32 错误码格式化为“描述 (代码)”形式的中文系统信息。
std::string system_error_text(unsigned long Code);

/// 当前线程最近一次 Win32 错误的文本。
std::string last_error_text();

} // namespace scriptroom
