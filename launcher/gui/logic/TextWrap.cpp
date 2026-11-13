#include "TextWrap.h"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace scriptroom::gui {

namespace {

bool starts_character(char Byte) {
    return (static_cast<unsigned char>(Byte) & 0xC0U) != 0x80U;
}

/// 第 i 个字符起始处的字节偏移（i 从 0 起，至多到第 Limit 个字符）；
/// 文本不足 Limit 个字符时，末尾追加 Text.size()。所以前 i 个字符正好是 Text.substr(0, 返回值[i])。
std::vector<std::size_t> boundaries(std::string_view Text, std::size_t Limit) {
    std::vector<std::size_t> Offsets;
    for (std::size_t Index = 0; Index < Text.size(); ++Index) {
        if (starts_character(Text[Index])) {
            Offsets.push_back(Index);
            if (Offsets.size() > Limit) {
                return Offsets;
            }
        }
    }
    Offsets.push_back(Text.size());
    return Offsets;
}

bool is_break_after(char Byte) {
    return Byte == ' ' || Byte == '\\' || Byte == '/' || Byte == ',' || Byte == ';';
}

/// 不含换行的一段文本折成多行，追加到 Lines。
void wrap_paragraph(std::string_view Text, float MaxWidth, const TextMeasure &Measure,
                    std::vector<std::string_view> &Lines) {
    // 每个字符至少 1 像素宽，所以一行不会超过 MaxWidth 个字符，只需在这个范围内二分。
    const std::size_t Window = static_cast<std::size_t>(std::max(MaxWidth, 1.0f)) + 1;
    while (true) {
        if (Measure(Text) <= MaxWidth) {
            Lines.push_back(Text);
            return;
        }
        const std::vector<std::size_t> Starts = boundaries(Text, Window);
        // 在 Starts 的下标上二分：最大的 Fit 使得 Text.substr(0, Starts[Fit]) 放得下。
        std::size_t Low = 1;
        std::size_t High = Starts.size() - 1;
        while (Low < High) {
            const std::size_t Mid = (Low + High + 1) / 2;
            if (Measure(Text.substr(0, Starts[Mid])) <= MaxWidth) {
                Low = Mid;
            } else {
                High = Mid - 1;
            }
        }
        std::size_t Cut = Starts[Low];
        // 在后半行里找最近的可断位置，找不到就在字符边界硬折。
        for (std::size_t Probe = Cut; Probe > Cut / 2 && Probe > 1; --Probe) {
            if (is_break_after(Text[Probe - 1])) {
                Cut = Probe;
                break;
            }
        }
        Lines.push_back(Text.substr(0, Cut));
        Text.remove_prefix(Cut);
        if (Text.empty()) {
            return;
        }
    }
}

} // namespace

std::vector<std::string_view> wrap_text(std::string_view Text, float MaxWidth, const TextMeasure &Measure) {
    std::vector<std::string_view> Lines;
    while (true) {
        const std::size_t Break = Text.find('\n');
        std::string_view Paragraph = Text.substr(0, Break);
        if (Paragraph.ends_with('\r')) {
            Paragraph.remove_suffix(1);
        }
        if (Paragraph.empty()) {
            Lines.push_back(Paragraph);
        } else {
            wrap_paragraph(Paragraph, MaxWidth, Measure, Lines);
        }
        if (Break == std::string_view::npos) {
            break;
        }
        Text.remove_prefix(Break + 1);
    }
    return Lines;
}

} // namespace scriptroom::gui
