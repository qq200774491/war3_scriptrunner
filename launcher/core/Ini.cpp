#include "Ini.h"

#include "Error.h"
#include "Text.h"

#include <format>

namespace scriptroom {

namespace {

bool has_key_ignore_case(const std::map<std::string, std::string> &Fields, std::string_view Key) {
    for (const auto &Entry : Fields) {
        if (equals_ignore_case(Entry.first, Key)) {
            return true;
        }
    }
    return false;
}

std::string decode_utf16(std::span<const std::uint8_t> Bytes) {
    require(Bytes.size() % 2 == 0, "INI UTF-16 不完整");
    const bool Little = Bytes[0] == 0xFF;
    std::wstring Wide;
    Wide.reserve(Bytes.size() / 2);
    for (std::size_t Index = 2; Index + 1 < Bytes.size() + 0; Index += 2) {
        const std::uint8_t A = Bytes[Index];
        const std::uint8_t B = Bytes[Index + 1];
        Wide.push_back(static_cast<wchar_t>(Little ? (A | (B << 8)) : (B | (A << 8))));
    }
    return to_utf8(Wide);
}

/// 与 Rust str::lines 一致：按 \n 分行并去掉行尾的 \r。
std::vector<std::string_view> split_lines(std::string_view Text) {
    std::vector<std::string_view> Lines;
    std::size_t Begin = 0;
    while (Begin < Text.size()) {
        std::size_t End = Text.find('\n', Begin);
        const bool Last = End == std::string_view::npos;
        std::string_view Line = Text.substr(Begin, Last ? std::string_view::npos : End - Begin);
        if (!Line.empty() && Line.back() == '\r') {
            Line.remove_suffix(1);
        }
        Lines.push_back(Line);
        if (Last) {
            break;
        }
        Begin = End + 1;
    }
    return Lines;
}

} // namespace

Ini Ini::parse(std::span<const std::uint8_t> Bytes) {
    const bool Utf16 = Bytes.size() >= 2 &&
                       ((Bytes[0] == 0xFF && Bytes[1] == 0xFE) || (Bytes[0] == 0xFE && Bytes[1] == 0xFF));
    if (Utf16) {
        return parse_text(decode_utf16(Bytes));
    }
    if (Bytes.size() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF) {
        const std::string_view Body(reinterpret_cast<const char *>(Bytes.data()) + 3, Bytes.size() - 3);
        require(is_valid_utf8(Body), "INI 不是有效的 UTF-8");
        return parse_text(Body);
    }
    return parse_text(decode_ansi(Bytes));
}

Ini Ini::parse_text(std::string_view Text) {
    require(Text.find('\0') == std::string_view::npos, "INI 包含空字符");
    Ini Result;
    std::string Section;
    bool InSection = false;
    for (const std::string_view Original : split_lines(Text)) {
        const std::string_view Line = trim(Original);
        if (Line.empty() || Line.front() == ';') {
            continue;
        }
        if (Line.front() == '[' && Line.back() == ']') {
            const std::string_view Name = Line.substr(1, Line.size() - 2);
            require(!Name.empty() && !equals_ignore_case(Name, "DEFAULT"), "INI 不支持空节或 DEFAULT 继承");
            for (const auto &Existing : Result.Sections) {
                require(!equals_ignore_case(Existing.first, Name), std::format("INI 重复节：{}", Name));
            }
            Section = std::string(Name);
            InSection = true;
            Result.Sections.emplace(Section, Fields{});
            continue;
        }
        const std::size_t Equals = Line.find('=');
        require(Equals != std::string_view::npos, "INI 包含非法行或多行值");
        const std::string_view Key = trim(Line.substr(0, Equals));
        require(!Key.empty(), "INI 空键");
        require(InSection, "INI 字段不在任何节内");
        Fields &Target = Result.Sections[Section];
        require(!has_key_ignore_case(Target, Key), std::format("INI 重复键：{}", Key));
        Target.emplace(std::string(Key), std::string(trim(Line.substr(Equals + 1))));
    }
    return Result;
}

Ini::Fields &Ini::section_mut(std::string_view Name) {
    for (auto &Entry : Sections) {
        if (equals_ignore_case(Entry.first, Name)) {
            return Entry.second;
        }
    }
    return Sections[std::string(Name)];
}

std::optional<std::string> Ini::get(std::string_view Section, std::string_view Key) const {
    for (const auto &SectionEntry : Sections) {
        if (!equals_ignore_case(SectionEntry.first, Section)) {
            continue;
        }
        for (const auto &Field : SectionEntry.second) {
            if (equals_ignore_case(Field.first, Key)) {
                return Field.second;
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::string Ini::text() const {
    std::string Result;
    for (const auto &SectionEntry : Sections) {
        Result += '[' + SectionEntry.first + "]\r\n";
        for (const auto &Field : SectionEntry.second) {
            Result += Field.first + '=' + Field.second + "\r\n";
        }
        Result += "\r\n";
    }
    return Result;
}

std::string Ini::utf16() const {
    const std::wstring Wide = to_wide(text());
    std::string Bytes = "\xFF\xFE";
    Bytes.reserve(Bytes.size() + Wide.size() * 2);
    for (const wchar_t Unit : Wide) {
        Bytes.push_back(static_cast<char>(Unit & 0xFF));
        Bytes.push_back(static_cast<char>((Unit >> 8) & 0xFF));
    }
    return Bytes;
}

} // namespace scriptroom
