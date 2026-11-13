#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace scriptroom {

/// 严格的 INI 文档：拒绝重复节/键、空节、DEFAULT 继承、多行值，
/// 避免用含糊的存档数据覆盖玩家进度。节与键按字节序排序输出。
class Ini {
public:
    using Fields = std::map<std::string, std::string>;

    /// 识别 UTF-16 BOM、UTF-8 BOM，其余按系统 ANSI 代码页解码。
    static Ini parse(std::span<const std::uint8_t> Bytes);
    static Ini parse_text(std::string_view Text);

    /// 返回节（大小写不敏感匹配已有节名），不存在则创建。
    Fields &section_mut(std::string_view Name);

    std::optional<std::string> get(std::string_view Section, std::string_view Key) const;

    /// CRLF 换行的文本形式。
    std::string text() const;

    /// 带 FF FE 前缀的 UTF-16LE 字节，魔兽插件读取的格式。
    std::string utf16() const;

private:
    std::map<std::string, Fields> Sections;
};

} // namespace scriptroom
