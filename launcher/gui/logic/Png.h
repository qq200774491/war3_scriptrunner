#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace scriptroom::gui {

/// 把 0x00RRGGBB 像素（自上而下、行紧密排列）编码成 8 位 RGB 的 PNG。
/// 压缩使用 zlib 的 stored 块：不压缩，换来实现极简且结果确定。
/// Pixels 的长度必须等于 Width * Height，否则抛出 Error。
std::vector<std::uint8_t> encode_png(int Width, int Height, std::span<const std::uint32_t> Pixels);

/// 编码并原子写入文件。
void write_png(const std::filesystem::path &Path, int Width, int Height, std::span<const std::uint32_t> Pixels);

} // namespace scriptroom::gui
