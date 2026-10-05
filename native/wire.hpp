#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace room {
constexpr std::uint32_t max_frame = 1024 * 1024;
using Bytes = std::vector<std::uint8_t>;

class Packet final {
public:
    explicit Packet(std::uint8_t kind);
    Packet& number(std::uint32_t value);
    Packet& sequence(std::uint64_t value);
    Packet& text(const std::string& value);
    void send() const;
private:
    Bytes bytes_;
};

class Reader final {
public:
    explicit Reader(const Bytes& bytes) : bytes_(bytes) {}
    std::uint8_t byte();
    std::uint32_t number();
    std::uint64_t sequence();
    std::string text();
    void finish() const;
private:
    const Bytes& bytes_;
    std::size_t position_ = 0;
};

// 非阻塞采集匿名管道数据，由主线程解析后调用 DLL。
class InputPipe final {
public:
    bool poll();
    bool next(Bytes& frame);
private:
    Bytes pending_;
};
}
