#include "connection.hpp"
#include "lua_api.hpp"
#include <memory>
#include <array>
#include <cstdio>

namespace {
constexpr const char* metatable = "shared_room.connection.v1";
std::unique_ptr<LuaApi> api;
struct Slot { std::unique_ptr<bridge::Connection> connection; };
Slot& slot(lua_State* state) { return *static_cast<Slot*>(api->checkudata(state, 1, metatable)); }
bridge::Connection& connection(lua_State* state) {
    auto& object = slot(state);
    if (!object.connection) throw std::runtime_error("connection already closed");
    return *object.connection;
}
std::string_view text(lua_State* state, int index) {
    std::size_t length = 0;
    auto data = api->checkstring(state, index, &length);
    return {data, length};
}
int open(lua_State* state) {
    const auto port = api->checkinteger(state, 1);
    if (port <= 0 || port > 65535) throw std::runtime_error("invalid port");
    auto* object = new(api->newuserdata(state, sizeof(Slot))) Slot;
    api->newmetatable(state, metatable);
    api->setmetatable(state, -2);
    object->connection = std::make_unique<bridge::Connection>(static_cast<unsigned short>(port));
    return 1;
}
int join(lua_State* state) {
    auto& object = connection(state);
    const auto room = text(state, 2);
    const auto player = api->checkinteger(state, 3);
    const auto token = text(state, 4);
    if (player < 0 || player >= 24) throw std::runtime_error("invalid player");
    object.join(room, static_cast<int>(player), token);
    return 0;
}
int send(lua_State* state) {
    auto& object = connection(state);
    const auto id = text(state, 2), event = text(state, 3), payload = text(state, 4);
    object.send(id, event, payload);
    return 0;
}
int poll(lua_State* state) {
    const auto* line = connection(state).poll();
    if (line) api->pushstring(state, line->data(), line->size());
    else api->pushnil(state);
    return 1;
}
int close(lua_State* state) { slot(state).connection.reset(); return 0; }
// close 与 GC 可重复调用；释放资源，不重复结束 userdata 内对象的生命周期。
int collect(lua_State* state) { slot(state).connection.reset(); return 0; }

template<LuaFunction Function>
int __cdecl boundary(lua_State* state) {
    std::array<char, 512> message{};
    try { return Function(state); }
    catch (const std::exception& error) { snprintf(message.data(), message.size(), "%s", error.what()); }
    catch (...) { snprintf(message.data(), message.size(), "%s", "native bridge failure"); }
    // 离开 C++ catch 后才执行 Lua longjmp；资源都归 userdata/Connection 所有。
    return api->error(state, "%s", message.data());
}
void function(lua_State* state, const char* name, LuaFunction callback) {
    api->pushclosure(state, callback, 0);
    api->setfield(state, -2, name);
}
}

extern "C" __declspec(dllexport) int __cdecl luaopen_shared_room_native(lua_State* state) {
    if (!api) {
        try {
            auto core = GetModuleHandleW(L"luacore.dll");
            if (!core) return 0;
            api = std::make_unique<LuaApi>(core);
        } catch (...) { return 0; }
    }
    api->newmetatable(state, metatable);
    api->pushvalue(state, -1);
    api->setfield(state, -2, "__index");
    api->pushstring(state, metatable, std::char_traits<char>::length(metatable));
    api->setfield(state, -2, "__metatable");
    function(state, "join", &boundary<join>);
    function(state, "send", &boundary<send>);
    function(state, "poll", &boundary<poll>);
    function(state, "close", &boundary<close>);
    function(state, "__gc", &boundary<collect>);
    api->createtable(state, 0, 1);
    function(state, "open", &boundary<open>);
    return 1;
}
