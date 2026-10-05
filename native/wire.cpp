#include "wire.hpp"
#include <Windows.h>
#include <array>
#include <stdexcept>

namespace room {
namespace {
void write_all(const void* data, std::size_t length) {
    const auto* cursor = static_cast<const std::uint8_t*>(data);
    while (length) {
        DWORD written = 0;
        if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), cursor,
                       static_cast<DWORD>(length), &written, nullptr) || !written)
            throw std::runtime_error("output pipe closed");
        cursor += written;
        length -= written;
    }
}
std::uint32_t read_number(const std::uint8_t* data) {
    return data[0] | (std::uint32_t(data[1]) << 8) |
        (std::uint32_t(data[2]) << 16) | (std::uint32_t(data[3]) << 24);
}
}

Packet::Packet(std::uint8_t kind) : bytes_{kind} {}
Packet& Packet::number(std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes_.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    return *this;
}
Packet& Packet::sequence(std::uint64_t value) {
    number(static_cast<std::uint32_t>(value));
    return number(static_cast<std::uint32_t>(value >> 32));
}
Packet& Packet::text(const std::string& value) {
    if (value.size() > max_frame) throw std::runtime_error("text too long");
    number(static_cast<std::uint32_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
    return *this;
}
void Packet::send() const {
    if (bytes_.size() > max_frame) throw std::runtime_error("frame too long");
    const auto length = static_cast<std::uint32_t>(bytes_.size());
    write_all(&length, sizeof(length));
    write_all(bytes_.data(), bytes_.size());
}
std::uint8_t Reader::byte() {
    if (position_ >= bytes_.size()) throw std::runtime_error("truncated frame");
    return bytes_[position_++];
}
std::uint32_t Reader::number() {
    if (bytes_.size() - position_ < 4) throw std::runtime_error("truncated integer");
    auto result = read_number(bytes_.data() + position_);
    position_ += 4;
    return result;
}
std::uint64_t Reader::sequence() {
    const auto low = number();
    return low | (std::uint64_t(number()) << 32);
}
std::string Reader::text() {
    auto length = number();
    if (length > bytes_.size() - position_) throw std::runtime_error("truncated string");
    std::string result(bytes_.begin() + position_, bytes_.begin() + position_ + length);
    position_ += length;
    if (result.find('\0') != std::string::npos) throw std::runtime_error("embedded NUL");
    return result;
}
void Reader::finish() const {
    if (position_ != bytes_.size()) throw std::runtime_error("trailing bytes");
}
bool InputPipe::poll() {
    const auto pipe = GetStdHandle(STD_INPUT_HANDLE);
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
        if (GetLastError() == ERROR_BROKEN_PIPE) return false;
        throw std::runtime_error("stdin must be an anonymous pipe");
    }
    if (!available) return true;
    std::array<std::uint8_t, 4096> buffer{};
    DWORD received = 0;
    if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &received, nullptr))
        throw std::runtime_error("input pipe failed");
    pending_.insert(pending_.end(), buffer.begin(), buffer.begin() + received);
    if (pending_.size() > max_frame + 4096) throw std::runtime_error("input buffer overflow");
    return true;
}
bool InputPipe::next(Bytes& frame) {
    if (pending_.size() < 4) return false;
    auto length = read_number(pending_.data());
    if (!length || length > max_frame) throw std::runtime_error("invalid frame length");
    if (pending_.size() < length + 4) return false;
    frame.assign(pending_.begin() + 4, pending_.begin() + 4 + length);
    pending_.erase(pending_.begin(), pending_.begin() + 4 + length);
    return true;
}
}
