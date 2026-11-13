#include "lua_api.hpp"

namespace {
template<class T>
T symbol(HMODULE module, const char* name) {
    auto address = GetProcAddress(module, name);
    if (!address) throw std::runtime_error(name);
    return reinterpret_cast<T>(address);
}
}
LuaApi::LuaApi(HMODULE core) {
    auto version = symbol<const double* (__cdecl*)(lua_State*)>(core, "lua_version");
    if (*version(nullptr) != 503) throw std::runtime_error("Lua 5.3 required");
    newuserdata = symbol<decltype(newuserdata)>(core, "lua_newuserdata");
    checkudata = symbol<decltype(checkudata)>(core, "luaL_checkudata");
    checkinteger = symbol<decltype(checkinteger)>(core, "luaL_checkinteger");
    checkstring = symbol<decltype(checkstring)>(core, "luaL_checklstring");
    newmetatable = symbol<decltype(newmetatable)>(core, "luaL_newmetatable");
    pushclosure = symbol<decltype(pushclosure)>(core, "lua_pushcclosure");
    pushvalue = symbol<decltype(pushvalue)>(core, "lua_pushvalue");
    setfield = symbol<decltype(setfield)>(core, "lua_setfield");
    setmetatable = symbol<decltype(setmetatable)>(core, "lua_setmetatable");
    createtable = symbol<decltype(createtable)>(core, "lua_createtable");
    pushnil = symbol<decltype(pushnil)>(core, "lua_pushnil");
    pushstring = symbol<decltype(pushstring)>(core, "lua_pushlstring");
    error = symbol<decltype(error)>(core, "luaL_error");
}
