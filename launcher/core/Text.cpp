#include "Text.h"

#include "Error.h"

#include <charconv>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace scriptroom {

std::wstring to_wide(std::string_view Utf8) {
    if (Utf8.empty()) {
        return {};
    }
    const int Count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Utf8.data(),
                                          static_cast<int>(Utf8.size()), nullptr, 0);
    require(Count > 0, "文本不是有效的 UTF-8");
    std::wstring Result(static_cast<std::size_t>(Count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Utf8.data(), static_cast<int>(Utf8.size()),
                        Result.data(), Count);
    return Result;
}

std::string to_utf8(std::wstring_view Wide) {
    if (Wide.empty()) {
        return {};
    }
    const int Count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Wide.data(),
                                          static_cast<int>(Wide.size()), nullptr, 0, nullptr,
                                          nullptr);
    require(Count > 0, "文本不是有效的 UTF-16");
    std::string Result(static_cast<std::size_t>(Count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Wide.data(), static_cast<int>(Wide.size()),
                        Result.data(), Count, nullptr, nullptr);
    return Result;
}

std::string path_to_utf8(const std::filesystem::path &Path) {
    const std::wstring &Native = Path.native();
    if (Native.empty()) {
        return {};
    }
    const int Count = WideCharToMultiByte(CP_UTF8, 0, Native.data(),
                                          static_cast<int>(Native.size()), nullptr, 0, nullptr,
                                          nullptr);
    std::string Result(static_cast<std::size_t>(Count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, Native.data(), static_cast<int>(Native.size()), Result.data(),
                        Count, nullptr, nullptr);
    return Result;
}

std::filesystem::path path_from_utf8(std::string_view Utf8) {
    return std::filesystem::path(to_wide(Utf8));
}

namespace {

/// 判断以 Index 开头的 UTF-8 序列；合法时返回其长度，非法时返回 0 并在 Invalid 给出应替换的字节数。
std::size_t sequence_length(std::string_view Bytes, std::size_t Index, std::size_t &Invalid) {
    const auto At = [&](std::size_t Offset) {
        return static_cast<unsigned char>(Bytes[Index + Offset]);
    };
    const std::size_t Left = Bytes.size() - Index;
    const unsigned char First = At(0);
    Invalid = 1;
    if (First < 0x80) {
        return 1;
    }
    std::size_t Length = 0;
    unsigned char SecondLow = 0x80;
    unsigned char SecondHigh = 0xBF;
    if (First >= 0xC2 && First <= 0xDF) {
        Length = 2;
    } else if (First >= 0xE0 && First <= 0xEF) {
        Length = 3;
        if (First == 0xE0) {
            SecondLow = 0xA0;
        }
        if (First == 0xED) {
            SecondHigh = 0x9F;
        }
    } else if (First >= 0xF0 && First <= 0xF4) {
        Length = 4;
        if (First == 0xF0) {
            SecondLow = 0x90;
        }
        if (First == 0xF4) {
            SecondHigh = 0x8F;
        }
    } else {
        return 0;
    }
    if (Left < 2 || At(1) < SecondLow || At(1) > SecondHigh) {
        return 0;
    }
    for (std::size_t Offset = 2; Offset < Length; ++Offset) {
        if (Left <= Offset || (At(Offset) & 0xC0) != 0x80) {
            Invalid = Offset;
            return 0;
        }
    }
    return Length;
}

} // namespace

bool is_valid_utf8(std::string_view Bytes) {
    std::size_t Index = 0;
    std::size_t Invalid = 0;
    while (Index < Bytes.size()) {
        const std::size_t Length = sequence_length(Bytes, Index, Invalid);
        if (Length == 0) {
            return false;
        }
        Index += Length;
    }
    return true;
}

std::string utf8_lossy(std::string_view Bytes) {
    std::string Result;
    Result.reserve(Bytes.size());
    std::size_t Index = 0;
    std::size_t Invalid = 0;
    while (Index < Bytes.size()) {
        const std::size_t Length = sequence_length(Bytes, Index, Invalid);
        if (Length == 0) {
            Result += "\xEF\xBF\xBD";
            Index += Invalid;
            continue;
        }
        Result.append(Bytes.substr(Index, Length));
        Index += Length;
    }
    return Result;
}

std::string decode_ansi(std::span<const std::uint8_t> Bytes) {
    if (Bytes.empty()) {
        return {};
    }
    const int Length = static_cast<int>(Bytes.size());
    const int Count = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS,
                                          reinterpret_cast<const char *>(Bytes.data()), Length,
                                          nullptr, 0);
    require(Count > 0, "INI 不是有效的系统代码页文本");
    std::wstring Wide(static_cast<std::size_t>(Count), L'\0');
    MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, reinterpret_cast<const char *>(Bytes.data()),
                        Length, Wide.data(), Count);
    return to_utf8(Wide);
}

std::string_view trim(std::string_view Text) {
    constexpr std::string_view Blank = " \t\r\n\v\f";
    const std::size_t Begin = Text.find_first_not_of(Blank);
    if (Begin == std::string_view::npos) {
        return {};
    }
    const std::size_t End = Text.find_last_not_of(Blank);
    return Text.substr(Begin, End - Begin + 1);
}

bool equals_ignore_case(std::string_view Left, std::string_view Right) {
    if (Left.size() != Right.size()) {
        return false;
    }
    for (std::size_t Index = 0; Index < Left.size(); ++Index) {
        char A = Left[Index];
        char B = Right[Index];
        if (A >= 'A' && A <= 'Z') {
            A = static_cast<char>(A + 32);
        }
        if (B >= 'A' && B <= 'Z') {
            B = static_cast<char>(B + 32);
        }
        if (A != B) {
            return false;
        }
    }
    return true;
}

bool starts_with_ignore_case(std::string_view Text, std::string_view Prefix) {
    return Text.size() >= Prefix.size() &&
           equals_ignore_case(Text.substr(0, Prefix.size()), Prefix);
}

std::string to_lower(std::string_view Utf8) {
    if (Utf8.empty()) {
        return {};
    }
    const std::wstring Wide = to_wide(utf8_lossy(Utf8));
    std::wstring Lower(Wide.size(), L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, Wide.data(),
                  static_cast<int>(Wide.size()), Lower.data(), static_cast<int>(Lower.size()),
                  nullptr, nullptr, 0);
    return to_utf8(Lower);
}

namespace {

template <typename T> std::optional<T> parse_number(std::string_view Text) {
    if (!Text.empty() && Text.front() == '+') {
        Text.remove_prefix(1);
    }
    if (Text.empty()) {
        return std::nullopt;
    }
    T Value{};
    const auto [End, Code] = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
    if (Code != std::errc() || End != Text.data() + Text.size()) {
        return std::nullopt;
    }
    return Value;
}

} // namespace

std::optional<std::int64_t> parse_i64(std::string_view Text) {
    return parse_number<std::int64_t>(Text);
}

std::optional<std::uint64_t> parse_u64(std::string_view Text) {
    return parse_number<std::uint64_t>(Text);
}

std::optional<double> parse_double(std::string_view Text) {
    return parse_number<double>(Text);
}

std::string_view strip_bom(std::string_view Text) {
    if (Text.starts_with("\xEF\xBB\xBF")) {
        Text.remove_prefix(3);
    }
    return Text;
}

std::string hex_encode(std::span<const std::uint8_t> Bytes) {
    constexpr char Digits[] = "0123456789abcdef";
    std::string Result;
    Result.reserve(Bytes.size() * 2);
    for (const std::uint8_t Byte : Bytes) {
        Result.push_back(Digits[Byte >> 4]);
        Result.push_back(Digits[Byte & 0x0F]);
    }
    return Result;
}

} // namespace scriptroom
