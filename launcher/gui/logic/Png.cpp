#include "Png.h"

#include "Error.h"
#include "FileSystem.h"

#include <algorithm>
#include <array>

namespace scriptroom::gui {

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t MaxStoredBlock = 65535;

std::array<std::uint32_t, 256> crc_table() {
    std::array<std::uint32_t, 256> Table{};
    for (std::uint32_t Index = 0; Index < Table.size(); ++Index) {
        std::uint32_t Value = Index;
        for (int Bit = 0; Bit < 8; ++Bit) {
            Value = (Value & 1U) != 0 ? 0xEDB88320U ^ (Value >> 1) : Value >> 1;
        }
        Table[Index] = Value;
    }
    return Table;
}

std::uint32_t crc32(std::span<const std::uint8_t> Data) {
    static const std::array<std::uint32_t, 256> Table = crc_table();
    std::uint32_t Value = 0xFFFFFFFFU;
    for (const std::uint8_t Byte : Data) {
        Value = Table[(Value ^ Byte) & 0xFFU] ^ (Value >> 8);
    }
    return ~Value;
}

std::uint32_t adler32(std::span<const std::uint8_t> Data) {
    constexpr std::uint32_t Modulus = 65521;
    std::uint32_t A = 1;
    std::uint32_t B = 0;
    for (const std::uint8_t Byte : Data) {
        A = (A + Byte) % Modulus;
        B = (B + A) % Modulus;
    }
    return (B << 16) | A;
}

void put_u32_be(Bytes &Out, std::uint32_t Value) {
    for (int Shift = 24; Shift >= 0; Shift -= 8) {
        Out.push_back(static_cast<std::uint8_t>(Value >> Shift));
    }
}

void put_chunk(Bytes &Out, const char (&Type)[5], const Bytes &Body) {
    put_u32_be(Out, static_cast<std::uint32_t>(Body.size()));
    const std::size_t CheckedFrom = Out.size();
    Out.insert(Out.end(), Type, Type + 4);
    Out.insert(Out.end(), Body.begin(), Body.end());
    put_u32_be(Out, crc32(std::span<const std::uint8_t>(Out).subspan(CheckedFrom)));
}

/// 每行前加滤波类型 0（不滤波），再把 RGB 三字节依次排开。
Bytes scanlines(int Width, int Height, std::span<const std::uint32_t> Pixels) {
    Bytes Raw;
    Raw.reserve(static_cast<std::size_t>(Height) * (static_cast<std::size_t>(Width) * 3 + 1));
    for (int Row = 0; Row < Height; ++Row) {
        Raw.push_back(0);
        for (int Column = 0; Column < Width; ++Column) {
            const std::uint32_t Pixel = Pixels[static_cast<std::size_t>(Row) * static_cast<std::size_t>(Width) +
                                               static_cast<std::size_t>(Column)];
            Raw.push_back(static_cast<std::uint8_t>(Pixel >> 16));
            Raw.push_back(static_cast<std::uint8_t>(Pixel >> 8));
            Raw.push_back(static_cast<std::uint8_t>(Pixel));
        }
    }
    return Raw;
}

/// zlib 头 + 若干 stored 块 + Adler-32。
Bytes zlib_stored(const Bytes &Raw) {
    Bytes Out = {0x78, 0x01};
    std::size_t Offset = 0;
    do {
        const std::size_t Length = std::min(MaxStoredBlock, Raw.size() - Offset);
        const bool Last = Offset + Length == Raw.size();
        Out.push_back(Last ? 1 : 0);
        Out.push_back(static_cast<std::uint8_t>(Length));
        Out.push_back(static_cast<std::uint8_t>(Length >> 8));
        Out.push_back(static_cast<std::uint8_t>(~Length));
        Out.push_back(static_cast<std::uint8_t>(~Length >> 8));
        Out.insert(Out.end(), Raw.begin() + static_cast<std::ptrdiff_t>(Offset),
                   Raw.begin() + static_cast<std::ptrdiff_t>(Offset + Length));
        Offset += Length;
    } while (Offset < Raw.size());
    put_u32_be(Out, adler32(Raw));
    return Out;
}

} // namespace

std::vector<std::uint8_t> encode_png(int Width, int Height, std::span<const std::uint32_t> Pixels) {
    require(Width > 0 && Height > 0, "PNG 尺寸必须为正");
    require(Pixels.size() == static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height),
            "PNG 像素数量与尺寸不符");

    Bytes Out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Bytes Header;
    put_u32_be(Header, static_cast<std::uint32_t>(Width));
    put_u32_be(Header, static_cast<std::uint32_t>(Height));
    Header.insert(Header.end(), {8, 2, 0, 0, 0}); // 位深 8、真彩色、默认压缩/滤波、不隔行
    put_chunk(Out, "IHDR", Header);
    put_chunk(Out, "IDAT", zlib_stored(scanlines(Width, Height, Pixels)));
    put_chunk(Out, "IEND", {});
    return Out;
}

void write_png(const std::filesystem::path &Path, int Width, int Height, std::span<const std::uint32_t> Pixels) {
    const Bytes Data = encode_png(Width, Height, Pixels);
    atomic_write(Path, std::string_view(reinterpret_cast<const char *>(Data.data()), Data.size()));
}

} // namespace scriptroom::gui
