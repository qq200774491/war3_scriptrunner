#include "map_selection.hpp"
#include "session.hpp"
#include "callback_boundary.hpp"
#include <detours.h>
#include <cstring>
#include <stdexcept>
#include <string>

namespace quicktest {
namespace {
using LoadString = BOOL(WINAPI*)(const char*, const char*, DWORD, char*, DWORD);
LoadString original_load_string = nullptr;
std::string selected_map;
const Session* active_session = nullptr;
bool selection_logged = false;

BOOL WINAPI load_selected_map(const char* section, const char* key, DWORD flags, char* buffer, DWORD capacity) noexcept {
    if (!section || !key || _stricmp(section, "Warcraft III\\Map") != 0 || _stricmp(key, "lan_V1") != 0)
        return original_load_string(section, key, flags, buffer, capacity);
    return recoverable_callback(*active_session, [&]() -> BOOL {
        if (!buffer || capacity <= selected_map.size()) return FALSE;
        std::memcpy(buffer, selected_map.c_str(), selected_map.size() + 1);
        if (!selection_logged) {
            selection_logged = true;
            active_session->log("LAN_MAP_SELECTED " + selected_map);
        }
        return TRUE;
    });
}
}

void install_map_selection(std::uintptr_t load_string, const std::filesystem::path& map, const Session& session) {
    selected_map = map.string();
    if (selected_map.empty() || selected_map.size() >= MAX_PATH)
        throw std::runtime_error("invalid LAN map selection path");
    active_session = &session;
    original_load_string = reinterpret_cast<LoadString>(load_string);
    // 建房页面会异步恢复 lan_V1。若它仍指向旧地图，会与自动建房的地图争用加载器。
    if (DetourTransactionBegin() != NO_ERROR) throw std::runtime_error("cannot start LAN map selection hook");
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR ||
        DetourAttach(reinterpret_cast<PVOID*>(&original_load_string), load_selected_map) != NO_ERROR) {
        DetourTransactionAbort();
        throw std::runtime_error("cannot attach LAN map selection hook");
    }
    if (DetourTransactionCommit() != NO_ERROR) throw std::runtime_error("cannot commit LAN map selection hook");
}
}
