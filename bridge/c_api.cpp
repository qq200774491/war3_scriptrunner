#include "connection.hpp"
#include <array>
#include <cstdio>
#include <map>
#include <limits>
#include <stdexcept>

namespace {
std::map<unsigned, std::unique_ptr<bridge::Connection>> connections;
unsigned next_handle = 1;
thread_local std::array<char, 512> last_error{};

template<class Operation>
int boundary(Operation&& operation) noexcept {
    try { return operation(); }
    catch (const std::exception& error) { snprintf(last_error.data(), last_error.size(), "%s", error.what()); }
    catch (...) { snprintf(last_error.data(), last_error.size(), "%s", "native connection failure"); }
    return -1;
}
bridge::Connection& connection(unsigned handle) {
    return *connections.at(handle);
}
std::string_view text(const char* value, unsigned length) {
    if (!value || length > 256 * 1024) throw std::runtime_error("invalid native text");
    return {value, length};
}
}

// 纯 C 边界供不同 Lua 运行库通过 FFI 调用，不使用外部 Lua C API 操作其状态。
extern "C" {
__declspec(dllexport) const char* __cdecl sr_error() noexcept { return last_error.data(); }
__declspec(dllexport) int __cdecl sr_open(unsigned port) noexcept {
    return boundary([&] {
        if (!port || port > 65535 || next_handle >= static_cast<unsigned>(std::numeric_limits<int>::max()))
            throw std::runtime_error("invalid port or exhausted connection handles");
        const auto handle = next_handle++;
        connections.emplace(handle, std::make_unique<bridge::Connection>(static_cast<unsigned short>(port)));
        return static_cast<int>(handle);
    });
}
__declspec(dllexport) int __cdecl sr_join(unsigned handle, const char* room, unsigned room_length,
    int player, const char* token, unsigned token_length) noexcept {
    return boundary([&] {
        if (player < 0 || player >= 24) throw std::runtime_error("invalid player");
        connection(handle).join(text(room, room_length), player, text(token, token_length));
        return 0;
    });
}
__declspec(dllexport) int __cdecl sr_send(unsigned handle, const char* id, unsigned id_length,
    const char* event, unsigned event_length, const char* payload, unsigned payload_length) noexcept {
    return boundary([&] {
        connection(handle).send(text(id, id_length), text(event, event_length), text(payload, payload_length));
        return 0;
    });
}
__declspec(dllexport) int __cdecl sr_poll(unsigned handle, const char** data, unsigned* length) noexcept {
    return boundary([&] {
        if (!data || !length) throw std::runtime_error("null poll output");
        const auto* line = connection(handle).poll();
        *data = line ? line->data() : nullptr;
        *length = line ? static_cast<unsigned>(line->size()) : 0;
        return line ? 1 : 0;
    });
}
__declspec(dllexport) void __cdecl sr_close(unsigned handle) noexcept { connections.erase(handle); }
}
