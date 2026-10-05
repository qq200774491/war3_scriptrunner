#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

struct lua_State;
namespace {
template<class T> T symbol(HMODULE core, const char* name) {
    auto value = GetProcAddress(core, name);
    if (!value) throw std::runtime_error(name);
    return reinterpret_cast<T>(value);
}
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3) return 2;
    // 编辑器模块依赖同一 bin 下的辅助 DLL，搜索路径仅影响这个测试进程。
    const auto directory = std::filesystem::path(argv[1]).parent_path();
    if (!SetDllDirectoryW(directory.c_str())) return 3;
    auto module = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) return 3;
    int result = 1;
    try {
        auto create = symbol<lua_State*(__cdecl*)()>(module, "luaL_newstate");
        auto close = symbol<void(__cdecl*)(lua_State*)>(module, "lua_close");
        std::unique_ptr<lua_State, decltype(close)> state(create(), close);
        if (!state) throw std::runtime_error("Lua allocation failed");
        symbol<void(__cdecl*)(lua_State*)>(module, "luaL_openlibs")(state.get());
        auto load = symbol<int(__cdecl*)(lua_State*, const char*, const char*)>(module, "luaL_loadfilex");
        auto call = symbol<int(__cdecl*)(lua_State*, int, int, int, std::intptr_t, void*)>(module, "lua_pcallk");
        const auto path = std::filesystem::path(argv[2]).u8string();
        auto code = load(state.get(), path.c_str(), nullptr);
        if (!code) code = call(state.get(), 0, 0, 0, 0, nullptr);
        if (code) {
            auto text = symbol<const char*(__cdecl*)(lua_State*, int, std::size_t*)>(module, "lua_tolstring");
            std::cerr << text(state.get(), -1, nullptr) << '\n';
        } else result = 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    FreeLibrary(module);
    return result;
}
