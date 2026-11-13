#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace scriptroom {

/// UTF-8 与 UTF-16 互转；输入非法时抛出 Error。
std::wstring to_wide(std::string_view Utf8);
std::string to_utf8(std::wstring_view Wide);

/// 路径与 UTF-8 文本互转（std::filesystem 在 Windows 上以 UTF-16 保存路径）。
std::string path_to_utf8(const std::filesystem::path &Path);
std::filesystem::path path_from_utf8(std::string_view Utf8);

/// 严格校验 UTF-8（拒绝过长编码、代理项和越界码点）。
bool is_valid_utf8(std::string_view Bytes);

/// 把非法序列替换为 U+FFFD 的宽松解码。
std::string utf8_lossy(std::string_view Bytes);

/// 系统 ANSI 代码页字节转 UTF-8；含无效字节时抛出 Error。
std::string decode_ansi(std::span<const std::uint8_t> Bytes);

/// 去掉首尾 ASCII 空白。
std::string_view trim(std::string_view Text);

bool equals_ignore_case(std::string_view Left, std::string_view Right);
bool starts_with_ignore_case(std::string_view Text, std::string_view Prefix);

/// 小写化，覆盖非 ASCII 字符（日志搜索使用）。
std::string to_lower(std::string_view Utf8);

/// 严格十进制解析；含多余字符或溢出时返回 nullopt。
std::optional<std::int64_t> parse_i64(std::string_view Text);
std::optional<std::uint64_t> parse_u64(std::string_view Text);
std::optional<double> parse_double(std::string_view Text);

/// 去掉 UTF-8 BOM。
std::string_view strip_bom(std::string_view Text);

std::string hex_encode(std::span<const std::uint8_t> Bytes);

} // namespace scriptroom
