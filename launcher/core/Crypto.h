#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace scriptroom {

using Sha256Digest = std::array<std::uint8_t, 32>;

/// 一次性 SHA-256（Windows CNG）。
Sha256Digest sha256(std::span<const std::uint8_t> Data);
Sha256Digest sha256(std::string_view Data);

/// 增量 SHA-256，用于大文件。
class Sha256Stream {
public:
    Sha256Stream();
    ~Sha256Stream();
    Sha256Stream(const Sha256Stream &) = delete;
    Sha256Stream &operator=(const Sha256Stream &) = delete;

    void update(std::span<const std::uint8_t> Data);
    Sha256Digest finish();

private:
    struct State;
    State *Impl;
};

/// 系统密码学随机字节。
std::vector<std::uint8_t> random_bytes(std::size_t Count);

/// 小写十六进制随机令牌，长度为 ByteCount * 2。
std::string random_hex(std::size_t ByteCount);

/// 版本 4 的随机 UUID 文本。
std::string new_uuid_v4();

/// 恒定时间比较，用于令牌校验。
bool constant_time_equal(std::string_view Left, std::string_view Right);

} // namespace scriptroom
