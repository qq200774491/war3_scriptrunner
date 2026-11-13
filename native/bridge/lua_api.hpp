#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

struct lua_State;
using LuaFunction = int(__cdecl*)(lua_State*);
using LuaInteger = std::int64_t;

// 只使用 Lua 5.3 的导出 C API，不依赖 Lua 状态内部布局。
struct LuaApi final {
    void* (__cdecl* newuserdata)(lua_State*, std::size_t);
    void* (__cdecl* checkudata)(lua_State*, int, const char*);
    LuaInteger (__cdecl* checkinteger)(lua_State*, int);
    const char* (__cdecl* checkstring)(lua_State*, int, std::size_t*);
    int (__cdecl* newmetatable)(lua_State*, const char*);
    void (__cdecl* pushclosure)(lua_State*, LuaFunction, int);
    void (__cdecl* pushvalue)(lua_State*, int);
    void (__cdecl* setfield)(lua_State*, int, const char*);
    int (__cdecl* setmetatable)(lua_State*, int);
    void (__cdecl* createtable)(lua_State*, int, int);
    void (__cdecl* pushnil)(lua_State*);
    const char* (__cdecl* pushstring)(lua_State*, const char*, std::size_t);
    int (__cdecl* error)(lua_State*, const char*, ...);
    explicit LuaApi(HMODULE core);
};
