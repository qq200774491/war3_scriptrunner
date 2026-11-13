// 图形界面的渲染辅助：截图用的 PNG 编码、自动折行。

#include "Png.h"
#include "TestHarness.h"
#include "TextWrap.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace scriptroom::gui;
using namespace testing;

std::uint32_t reference_crc32(const std::uint8_t *Data, std::size_t Size) {
    std::uint32_t Value = 0xFFFFFFFFU;
    for (std::size_t Index = 0; Index < Size; ++Index) {
        Value ^= Data[Index];
        for (int Bit = 0; Bit < 8; ++Bit) {
            Value = (Value & 1U) != 0 ? (Value >> 1) ^ 0xEDB88320U : Value >> 1;
        }
    }
    return ~Value;
}

std::uint32_t read_u32(const std::vector<std::uint8_t> &Data, std::size_t Offset) {
    return (std::uint32_t(Data[Offset]) << 24) | (std::uint32_t(Data[Offset + 1]) << 16) |
           (std::uint32_t(Data[Offset + 2]) << 8) | std::uint32_t(Data[Offset + 3]);
}

struct DecodedPng {
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::vector<std::uint8_t> Raw; ///< 解压后的扫描线（含每行的滤波字节）
};

/// 独立于被测代码的最小解码：逐块校验 CRC，并按 zlib stored 块还原数据与 Adler-32。
DecodedPng decode_stored_png(const std::vector<std::uint8_t> &Png) {
    const std::array<std::uint8_t, 8> Signature = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    check(Png.size() > 8);
    for (std::size_t Index = 0; Index < 8; ++Index) {
        check_equal(Png[Index], Signature[Index]);
    }
    DecodedPng Result;
    std::vector<std::uint8_t> Zlib;
    bool SawEnd = false;
    std::size_t Offset = 8;
    while (Offset < Png.size()) {
        const std::uint32_t Length = read_u32(Png, Offset);
        const std::string Type(Png.begin() + static_cast<std::ptrdiff_t>(Offset + 4),
                               Png.begin() + static_cast<std::ptrdiff_t>(Offset + 8));
        const std::size_t BodyStart = Offset + 8;
        check(BodyStart + Length + 4 <= Png.size());
        check_equal(read_u32(Png, BodyStart + Length), reference_crc32(Png.data() + Offset + 4, Length + 4));
        if (Type == "IHDR") {
            check_equal(Length, std::uint32_t(13));
            Result.Width = read_u32(Png, BodyStart);
            Result.Height = read_u32(Png, BodyStart + 4);
            check_equal(Png[BodyStart + 8], std::uint8_t(8)); // 位深
            check_equal(Png[BodyStart + 9], std::uint8_t(2)); // 真彩色
        } else if (Type == "IDAT") {
            Zlib.insert(Zlib.end(), Png.begin() + static_cast<std::ptrdiff_t>(BodyStart),
                        Png.begin() + static_cast<std::ptrdiff_t>(BodyStart + Length));
        } else if (Type == "IEND") {
            SawEnd = true;
        }
        Offset = BodyStart + Length + 4;
    }
    check(SawEnd);
    check_equal(Offset, Png.size());

    check(Zlib.size() > 6);
    check_equal(Zlib[0], std::uint8_t(0x78));
    check_equal((Zlib[0] * 256 + Zlib[1]) % 31, 0); // zlib 头校验位
    std::size_t Position = 2;
    bool Last = false;
    while (!Last) {
        check(Position + 5 <= Zlib.size());
        Last = (Zlib[Position] & 1U) != 0;
        check_equal(Zlib[Position] & 6U, 0U); // stored 块
        const std::size_t Length = Zlib[Position + 1] | (std::size_t(Zlib[Position + 2]) << 8);
        const std::size_t Inverse = Zlib[Position + 3] | (std::size_t(Zlib[Position + 4]) << 8);
        check_equal(Length ^ 0xFFFFU, Inverse);
        check(Position + 5 + Length <= Zlib.size());
        Result.Raw.insert(Result.Raw.end(), Zlib.begin() + static_cast<std::ptrdiff_t>(Position + 5),
                          Zlib.begin() + static_cast<std::ptrdiff_t>(Position + 5 + Length));
        Position += 5 + Length;
    }
    std::uint32_t A = 1;
    std::uint32_t B = 0;
    for (const std::uint8_t Byte : Result.Raw) {
        A = (A + Byte) % 65521;
        B = (B + A) % 65521;
    }
    check_equal(Position + 4, Zlib.size());
    check_equal(read_u32(Zlib, Position), (B << 16) | A);
    return Result;
}

const Registrar PngEncodesSmallImage("gui.png.small_image", [] {
    const std::vector<std::uint32_t> Pixels = {0x00FF0000, 0x0000FF00, 0x000000FF, 0x00102030, 0x00FFFFFF, 0x00000000};
    const DecodedPng Decoded = decode_stored_png(encode_png(3, 2, Pixels));
    check_equal(Decoded.Width, std::uint32_t(3));
    check_equal(Decoded.Height, std::uint32_t(2));
    check_equal(Decoded.Raw.size(), std::size_t(2 * (1 + 3 * 3)));
    // 第一行：滤波字节 0，随后 R G B 依次排列。
    const std::vector<std::uint8_t> FirstRow = {0, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF};
    check(std::vector<std::uint8_t>(Decoded.Raw.begin(), Decoded.Raw.begin() + 10) == FirstRow);
    check_equal(Decoded.Raw[10], std::uint8_t(0));
    check_equal(Decoded.Raw[11], std::uint8_t(0x10));
    check_equal(Decoded.Raw[13], std::uint8_t(0x30));
});

const Registrar PngSplitsLargeImageIntoBlocks("gui.png.large_image_uses_several_blocks", [] {
    constexpr int Width = 300;
    constexpr int Height = 300;
    std::vector<std::uint32_t> Pixels(Width * Height);
    for (std::size_t Index = 0; Index < Pixels.size(); ++Index) {
        Pixels[Index] = static_cast<std::uint32_t>((Index * 2654435761U) & 0xFFFFFFU);
    }
    const DecodedPng Decoded = decode_stored_png(encode_png(Width, Height, Pixels));
    check_equal(Decoded.Raw.size(), std::size_t(Height) * (std::size_t(Width) * 3 + 1)); // 大于 65535：多个 stored 块
    // 抽查最后一个像素。
    const std::size_t Tail = Decoded.Raw.size() - 3;
    const std::uint32_t Last = Pixels.back();
    check_equal(Decoded.Raw[Tail], std::uint8_t(Last >> 16));
    check_equal(Decoded.Raw[Tail + 1], std::uint8_t(Last >> 8));
    check_equal(Decoded.Raw[Tail + 2], std::uint8_t(Last));
});

const Registrar PngRejectsBadSize("gui.png.rejects_bad_size", [] {
    const std::vector<std::uint32_t> Pixels(5);
    check_throws([&] { encode_png(2, 2, Pixels); }, "像素数量");
    check_throws([&] { encode_png(0, 2, Pixels); }, "尺寸");
});

/// 每个字符（码点）10 像素宽的等宽度量。
float fixed_width(std::string_view Text) {
    float Width = 0;
    for (const char Byte : Text) {
        if ((static_cast<unsigned char>(Byte) & 0xC0U) != 0x80U) {
            Width += 10.0f;
        }
    }
    return Width;
}

std::string join(const std::vector<std::string_view> &Lines) {
    std::string Joined;
    for (const std::string_view Line : Lines) {
        Joined += Line;
    }
    return Joined;
}

const Registrar WrapBreaksAfterSeparators("gui.wrap.prefers_separators", [] {
    const std::vector<std::string_view> Lines = wrap_text("hello world foo", 80.0f, fixed_width);
    check_equal(Lines.size(), std::size_t(3));
    check_equal(std::string(Lines[0]), std::string("hello "));
    check_equal(std::string(Lines[1]), std::string("world "));
    check_equal(std::string(Lines[2]), std::string("foo"));

    const std::vector<std::string_view> Path = wrap_text("C:\\data\\scripts\\main.lua", 130.0f, fixed_width);
    check(Path.size() >= 2);
    check_equal(std::string(Path[0]), std::string("C:\\data\\"));
    check_equal(join(Path), std::string("C:\\data\\scripts\\main.lua"));
});

const Registrar WrapKeepsMultibyteCharacters("gui.wrap.never_splits_utf8", [] {
    const std::string Text = "云脚本房间任务执行中";
    const std::vector<std::string_view> Lines = wrap_text(Text, 40.0f, fixed_width); // 每行 4 个字
    check_equal(Lines.size(), std::size_t(3));
    for (const std::string_view Line : Lines) {
        check(fixed_width(Line) <= 40.0f);
    }
    check_equal(join(Lines), Text);
});

const Registrar WrapHandlesNewlinesAndTinyWidth("gui.wrap.newlines_and_narrow_width", [] {
    const std::vector<std::string_view> Lines = wrap_text("a\r\n\nb", 100.0f, fixed_width);
    check_equal(Lines.size(), std::size_t(3));
    check_equal(std::string(Lines[0]), std::string("a"));
    check_equal(std::string(Lines[1]), std::string(""));
    check_equal(std::string(Lines[2]), std::string("b"));

    // 宽度不足一个字符时每行仍至少放一个字，不会死循环。
    const std::vector<std::string_view> Narrow = wrap_text("abc", 3.0f, fixed_width);
    check_equal(Narrow.size(), std::size_t(3));
    check_equal(join(Narrow), std::string("abc"));

    check_equal(wrap_text("", 50.0f, fixed_width).size(), std::size_t(1)); // 空文本是一行空行
});

} // namespace
