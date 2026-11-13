#include "Crypto.h"

#include "Error.h"
#include "Text.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

namespace scriptroom {

namespace {

void check_status(NTSTATUS Status, const char *Action) {
    if (!BCRYPT_SUCCESS(Status)) {
        throw Error(std::string("系统密码学接口失败：") + Action);
    }
}

} // namespace

struct Sha256Stream::State {
    BCRYPT_ALG_HANDLE Algorithm = nullptr;
    BCRYPT_HASH_HANDLE Hash = nullptr;
    std::vector<UCHAR> Object;

    ~State() {
        if (Hash != nullptr) {
            BCryptDestroyHash(Hash);
        }
        if (Algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(Algorithm, 0);
        }
    }
};

Sha256Stream::Sha256Stream() : Impl(new State) {
    try {
        check_status(BCryptOpenAlgorithmProvider(&Impl->Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
                     "打开 SHA-256");
        DWORD ObjectSize = 0;
        DWORD Written = 0;
        check_status(BCryptGetProperty(Impl->Algorithm, BCRYPT_OBJECT_LENGTH,
                                       reinterpret_cast<PUCHAR>(&ObjectSize), sizeof(ObjectSize),
                                       &Written, 0),
                     "读取哈希对象大小");
        Impl->Object.resize(ObjectSize);
        check_status(BCryptCreateHash(Impl->Algorithm, &Impl->Hash, Impl->Object.data(),
                                      static_cast<ULONG>(Impl->Object.size()), nullptr, 0, 0),
                     "创建哈希");
    } catch (...) {
        delete Impl;
        throw;
    }
}

Sha256Stream::~Sha256Stream() {
    delete Impl;
}

void Sha256Stream::update(std::span<const std::uint8_t> Data) {
    if (Data.empty()) {
        return;
    }
    check_status(BCryptHashData(Impl->Hash, const_cast<PUCHAR>(Data.data()),
                                static_cast<ULONG>(Data.size()), 0),
                 "写入哈希数据");
}

Sha256Digest Sha256Stream::finish() {
    Sha256Digest Digest{};
    check_status(BCryptFinishHash(Impl->Hash, Digest.data(), static_cast<ULONG>(Digest.size()), 0),
                 "完成哈希");
    return Digest;
}

Sha256Digest sha256(std::span<const std::uint8_t> Data) {
    Sha256Stream Stream;
    Stream.update(Data);
    return Stream.finish();
}

Sha256Digest sha256(std::string_view Data) {
    return sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(Data.data()),
                                                Data.size()));
}

std::vector<std::uint8_t> random_bytes(std::size_t Count) {
    std::vector<std::uint8_t> Bytes(Count);
    if (Count > 0) {
        check_status(BCryptGenRandom(nullptr, Bytes.data(), static_cast<ULONG>(Count),
                                     BCRYPT_USE_SYSTEM_PREFERRED_RNG),
                     "生成随机数");
    }
    return Bytes;
}

std::string random_hex(std::size_t ByteCount) {
    return hex_encode(random_bytes(ByteCount));
}

std::string new_uuid_v4() {
    std::vector<std::uint8_t> Bytes = random_bytes(16);
    Bytes[6] = static_cast<std::uint8_t>((Bytes[6] & 0x0F) | 0x40);
    Bytes[8] = static_cast<std::uint8_t>((Bytes[8] & 0x3F) | 0x80);
    const std::string Hex = hex_encode(Bytes);
    return Hex.substr(0, 8) + "-" + Hex.substr(8, 4) + "-" + Hex.substr(12, 4) + "-" +
           Hex.substr(16, 4) + "-" + Hex.substr(20, 12);
}

bool constant_time_equal(std::string_view Left, std::string_view Right) {
    if (Left.size() != Right.size()) {
        return false;
    }
    unsigned char Difference = 0;
    for (std::size_t Index = 0; Index < Left.size(); ++Index) {
        Difference |= static_cast<unsigned char>(Left[Index] ^ Right[Index]);
    }
    return Difference == 0;
}

} // namespace scriptroom
