// 派生自 shuen4/war3_quicktest；上游 MIT 许可见同目录 LICENSE。
#include <winsock2.h>
#include <windows.h>
#include <detours.h>
#include "session.hpp"
#include "ini_redirect.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

template<class t = uint32_t>
inline t ReadMemory(uint32_t addr) {
    return *(t*)addr;
}
template<class t = uint32_t>
inline void WriteMemory(uint32_t addr, t value) {
    *(t*)addr = value;
}

// copied from GHost++ util.cpp
std::vector<uint8_t> EncodeGameDesc(std::vector<uint8_t>& data) {
    unsigned char Mask = 1;
    std::vector<uint8_t> Result;

    for (unsigned int i = 0; i < data.size(); ++i) {
        if ((data[i] % 2) == 0)
            Result.push_back(data[i] + 1);
        else {
            Result.push_back(data[i]);
            Mask |= 1 << ((i % 7) + 1);
        }

        if (i % 7 == 6 || i == data.size() - 1) {
            Result.insert(Result.end() - 1 - (i % 7), Mask);
            Mask = 1;
        }
    }

    return Result;
}

#pragma region storm
void(__stdcall* SRegLoadString)(const char*, const char*, uint32_t, char*, uint32_t);
void(__stdcall* SMemFree)(void*, const char*, uint32_t, uint32_t);
#pragma endregion

#pragma region war3
uint32_t base;

#define PROP_NET                            0xE
#define MAP_FOG_DEFAULT                     0x8
#define MAP_TEAMS_TOGETHER                  0x40
#define MAP_RESOURCE_TRADING_ALLIES_ONLY    0x200
#define MAP_LOCK_ALLIANCE_CHANGES           0x400
#define DEFAULT_MAP_FLAG                    (MAP_LOCK_ALLIANCE_CHANGES | MAP_RESOURCE_TRADING_ALLIES_ONLY | MAP_TEAMS_TOGETHER | MAP_FOG_DEFAULT)

struct GAMEDATA {
    char name[32];
    char password[16];
    char desc[128];
    uint32_t max_players;
    uint32_t category_id;
}; static_assert(sizeof(GAMEDATA) == 184);
struct CGlueMgr {
    uint32_t unk_0[97];
    uint32_t menu;
    uint32_t unk_392[9];
}; static_assert(sizeof(CGlueMgr) == 428);
struct NetProvider {
    uint32_t unk_0[9];
    uint32_t program;
    uint32_t version;
    uint32_t unk_44[379];
    uint32_t max_game;
    uint32_t unk_1564[9];
}; static_assert(sizeof(NetProvider) == 1600);
struct CGameChatroom {
    uint32_t unk_0[90];
    uint32_t session;
    uint32_t unk_1564[45];
}; static_assert(sizeof(CGameChatroom) == 544);
struct CNetGlueGameSetup {
    struct CEventDistFilesOutstanding {
        uint32_t unk_0[4];
        uint32_t count;
    }; static_assert(sizeof(CEventDistFilesOutstanding) == 20);
};
template <class T>
struct TSGrowableArray {
    uint32_t allocated_size;
    uint32_t size;
    T* data;
    uint32_t unk_12;

    ~TSGrowableArray() {
        if (data)
            SMemFree(data, __FILE__, __LINE__, 0);
    }
}; static_assert(sizeof(TSGrowableArray<void>) == 16);

uint32_t(__fastcall* PropGet)(uint32_t);

CGlueMgr* (__cdecl* CGlueMgr_Get)();
void(__fastcall* CGlueMgr_SetGlueScreen)(uint32_t, uint32_t);

void(__thiscall* CGameChatroom_OnDistFilesOutstanding)(CGameChatroom*, uint32_t);
void(__thiscall* CGameChatroom_OnPlayerJoin)(CGameChatroom*, uint32_t);

BOOL(__fastcall* WorldOpenMapArchive)(const char*);
BOOL(__fastcall* WorldGetMapInfo)(const char*, void*, TSGrowableArray<void>* players, void*, void*);

void(__fastcall* NetInitializeProvider)(uint32_t, uint32_t);
void(__fastcall* NetRegisterEvent)(uint32_t, uint32_t, uint32_t, uint32_t);
void(__fastcall* NetGlueGameCreate)(const char*, const char*, const char*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const char*, BOOL, BOOL, BOOL, uint32_t);
void(__thiscall* NetGlueGameJoin)(uint32_t, const char*, const char*, uint32_t);
void(__fastcall* NetStartGame)(uint32_t);
void(__fastcall* NetLeaveGame)(uint32_t, uint32_t, uint32_t);

uint32_t(__thiscall* Net_NetProvider_RemoteAdAdd)(NetProvider*, sockaddr_in*, sockaddr_in*, uint32_t, uint32_t, uint32_t, uint32_t, GAMEDATA*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

uint32_t(__fastcall* Net_RandomNumber)(uint32_t);

BOOL(__thiscall* CGameWar3_LoadMapSetup)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
#pragma endregion

quicktest::Session session;
char player_name[16];
bool join_pending = true;

#pragma region host
std::string filepath;
int player_count;
int player_joined = 1;
bool leave_is_error = false;
void(__fastcall* real_CGlueMgr_SetGlueScreen)(uint32_t, uint32_t);
bool host_pending = true;
constexpr UINT host_menu_warmup_ms = 5000;
void CALLBACK on_host_menu_ready(HWND, UINT, UINT_PTR timer, DWORD) try {
    KillTimer(nullptr, timer);
    session.log("HOST_CREATE_BEGIN");
    // open map
    if (!WorldOpenMapArchive(filepath.c_str())) {
        session.fail("Load map failed");
    }

    // get map info
    TSGrowableArray<void> players{};
    if (!WorldGetMapInfo(filepath.c_str(), NULL, &players, NULL, NULL)) {
        session.fail("Load map info failed");
    }

    // create game
    NetGlueGameCreate(
        "quicktest",
        "quicktest",
        filepath.c_str(),
        players.size,
        players.size,
        1, // game type
        2, // game speed
        DEFAULT_MAP_FLAG,
        player_name,
        false,
        false,
        false,
        0
    );

    // menu ui
    auto glue_mgr = CGlueMgr_Get();
    NetRegisterEvent(0x40090073, 6, glue_mgr->menu, 0);
    NetRegisterEvent(0x40090073, 6, glue_mgr->menu, 1);

    session.log("HOST_LOBBY");

    leave_is_error = true;

} catch (const std::exception& error) {
    session.fail(std::string("host create: ") + error.what());
} catch (...) {
    session.fail("unexpected host create exception");
}
void __fastcall fake_CGlueMgr_SetGlueScreen_host(uint32_t a1, uint32_t a2) try {
    if (a1 == 2 && host_pending) {
        host_pending = false;
        NetInitializeProvider('TCPN', 0);
        real_CGlueMgr_SetGlueScreen(9, 1);
        // 让初始菜单调用栈先退出，再由同一线程的消息循环发起建房。
        // 当前地图加载器需要菜单预热；实测仅推迟一个消息循环仍会退出。
        if (!SetTimer(nullptr, 0, host_menu_warmup_ms, on_host_menu_ready))
            session.fail("cannot schedule host creation");
        session.log("HOST_MENU_READY");
        return;
    }
    return real_CGlueMgr_SetGlueScreen(a1, a2);
} catch (const std::exception& error) {
    session.fail(std::string("host menu: ") + error.what());
} catch (...) {
    session.fail("unexpected host menu exception");
}
void(__thiscall* real_CGameChatroom_OnDistFilesOutstanding)(CGameChatroom*, CNetGlueGameSetup::CEventDistFilesOutstanding*);
void __fastcall fake_CGameChatroom_OnDistFilesOutstanding(CGameChatroom* _this, uint32_t, CNetGlueGameSetup::CEventDistFilesOutstanding* e) {
    real_CGameChatroom_OnDistFilesOutstanding(_this, e);
    // 地图配置返回不代表房主已进入房间；文件分发完成后才允许启动加入端。
    if (e->count == 0) session.ready();
    if (e->count == 0 && player_count == player_joined) {
        // start game
        session.log("START_GAME players=" + std::to_string(player_joined));
        NetStartGame(_this->session);
        // cleanup
        session.close_events();
        leave_is_error = false;
    }
}
void(__thiscall* real_CGameChatroom_OnPlayerJoin)(uint32_t, uint32_t);
void __fastcall fake_CGameChatroom_OnPlayerJoin(uint32_t a1, uint32_t, uint32_t a2) {
    real_CGameChatroom_OnPlayerJoin(a1, a2);
    player_joined++;
    session.log("PLAYER_JOINED count=" + std::to_string(player_joined));
}
uint32_t(__fastcall* real_Net_RandomNumber)(uint32_t);
uint32_t __fastcall fake_Net_RandomNumber(uint32_t) {
    return 'test';
}
BOOL(__thiscall* real_CGameWar3_LoadMapSetup)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
BOOL __fastcall fake_CGameWar3_LoadMapSetup(uint32_t a1, uint32_t, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    auto ret = real_CGameWar3_LoadMapSetup(a1, a2, a3, a4, a5);
    if (!ret) {
        session.fail("Load map setup failed");
    }
    session.log("MAP_SETUP_LOADED");

    return ret;
}
void(__fastcall* real_NetLeaveGame)(uint32_t, uint32_t, uint32_t);
void __fastcall fake_NetLeaveGame(uint32_t a1, uint32_t a2, uint32_t a3) {
    if (leave_is_error) {
        // 房间错误或用户取消只结束本轮启动，保留游戏窗口和原生错误提示。
        leave_is_error = false;
        session.report_failure("host left lobby before game start");
    }
    return real_NetLeaveGame(a1, a2, a3);
}
#pragma endregion

#pragma region join
void __fastcall fake_CGlueMgr_SetGlueScreen_join(uint32_t a1, uint32_t a2) try {
    // main menu
    if (a1 == 2 && join_pending) {
        join_pending = false;
        session.log("JOIN_REQUEST");
        // cleanup
        session.close_events();

        // initialize NetProvider
        NetInitializeProvider('TCPN', 0);

        // set to LAN menu
        real_CGlueMgr_SetGlueScreen(8, 1);

        // construct local server address
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.S_un.S_addr = 0x0100007F; // 127.0.0.1
        addr.sin_port = 0xE017; // 6112

        // construct raw game data;
        std::vector<uint8_t> raw_game_data;

        // game speed
        raw_game_data.push_back(2);
        // map flag
        raw_game_data.push_back((DEFAULT_MAP_FLAG >> 0) & 0xFF);
        raw_game_data.push_back((DEFAULT_MAP_FLAG >> 8) & 0xFF);
        raw_game_data.push_back((DEFAULT_MAP_FLAG >> 16) & 0xFF);
        raw_game_data.push_back((DEFAULT_MAP_FLAG >> 24) & 0xFF);

        // map width (not important)
        raw_game_data.push_back(0x00);
        raw_game_data.push_back(0x00);

        // map height (not important)
        raw_game_data.push_back(0x00);
        raw_game_data.push_back(0x00);

        // CRC (disable CRC check)
        raw_game_data.push_back(0xFF);
        raw_game_data.push_back(0xFF);
        raw_game_data.push_back(0xFF);
        raw_game_data.push_back(0xFF);

        // file path (not important)
        // 区间两端必须来自同一个数组，不能依赖编译器合并字符串字面量。
        static constexpr uint8_t placeholder[] = "quicktest";
        raw_game_data.insert(raw_game_data.end(), std::begin(placeholder), std::end(placeholder));

        // host name (not important)
        raw_game_data.insert(raw_game_data.end(), std::begin(placeholder), std::end(placeholder));

        // ???
        raw_game_data.push_back(0);

        // encode
        std::vector<uint8_t> game_desc = EncodeGameDesc(raw_game_data);

        // construct game data
        GAMEDATA game_data{};
        // game name (not important)
        strcpy_s(game_data.name, "quicktest");
        // encoded game data
        if (game_desc.size() >= sizeof(game_data.desc))
            throw std::runtime_error("encoded game description exceeds protocol limit");
        memcpy(game_data.desc, game_desc.data(), game_desc.size());

        // max player (not important)
        game_data.max_players = 12;
        // ???
        game_data.category_id = 0;

        // get net provider
        auto net = (NetProvider*)PropGet(PROP_NET);

        // set max game (default 0)
        net->max_game = 1;

        // add game and send join request
        NetGlueGameJoin(
            Net_NetProvider_RemoteAdAdd(
                net,
                &addr,
                &addr,
                1,                          // this should get from somewhere
                fake_Net_RandomNumber(0),   // must match host's Net::RandomNumber
                net->program,
                net->version,
                &game_data,
                1,                          // not important
                12,                         // not important
                0,                          // not important
                NULL,                       // ???
                1                           // ???
            ),
            "quicktest",
            player_name,
            0
        );

        // menu
        auto glue_mgr = CGlueMgr_Get();
        NetRegisterEvent(0x40090078, 6, glue_mgr->menu, 0);
        NetRegisterEvent(0x40090078, 6, glue_mgr->menu, 1);

        return;
    }
    return real_CGlueMgr_SetGlueScreen(a1, a2);
} catch (const std::exception& error) {
    session.fail(std::string("join menu: ") + error.what());
} catch (...) {
    session.fail("unexpected join menu exception");
}
#pragma endregion

bool init_game() {
    // get base address
    base = (uint32_t)GetModuleHandleA("Game.dll");
    if (!base)
        return false;

    // get timestamp (for version check)
    IMAGE_DOS_HEADER* dos_header = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt_header = (IMAGE_NT_HEADERS*)((uint32_t)dos_header + dos_header->e_lfanew);
    uint32_t timestap = nt_header->FileHeader.TimeDateStamp;

    switch (timestap) {
    case 0x56BD0E1C:
        // 1.27.0.52240
        WriteMemory((uint32_t)&SRegLoadString                           , base + 0x120640);
        WriteMemory((uint32_t)&SMemFree                                 , base + 0x1205CE);
        WriteMemory((uint32_t)&PropGet                                  , base + 0x04EFB0);
        WriteMemory((uint32_t)&CGlueMgr_Get                             , base + 0x2C0920);
        WriteMemory((uint32_t)&CGlueMgr_SetGlueScreen                   , base + 0x2E6520);
        WriteMemory((uint32_t)&CGameChatroom_OnDistFilesOutstanding     , base + 0x2CE620);
        WriteMemory((uint32_t)&CGameChatroom_OnPlayerJoin               , base + 0x2DA340);
        WriteMemory((uint32_t)&WorldOpenMapArchive                      , base + 0x76ED90);
        WriteMemory((uint32_t)&WorldGetMapInfo                          , base + 0x76E060);
        WriteMemory((uint32_t)&NetInitializeProvider                    , base + 0x30E610);
        WriteMemory((uint32_t)&Net_NetProvider_RemoteAdAdd              , base + 0x851290);
        WriteMemory((uint32_t)&NetGlueGameCreate                        , base + 0x283320);
        WriteMemory((uint32_t)&NetGlueGameJoin                          , base + 0x283430);
        WriteMemory((uint32_t)&NetRegisterEvent                         , base + 0x30EDA0);
        WriteMemory((uint32_t)&NetStartGame                             , base + 0x30F960);
        WriteMemory((uint32_t)&NetLeaveGame                             , base + 0x30E9C0);
        WriteMemory((uint32_t)&Net_RandomNumber                         , base + 0x845840);
        WriteMemory((uint32_t)&CGameWar3_LoadMapSetup                   , base + 0x1C60A0);
        break;
    default:
        return false;
    }

    strcpy_s(player_name, session.options().player.c_str());
    if (session.options().host) {
        player_count = session.options().players;
        filepath = session.options().map;
        // prepare hook function address
        WriteMemory((uint32_t)&real_CGlueMgr_SetGlueScreen                  , CGlueMgr_SetGlueScreen);
        WriteMemory((uint32_t)&real_CGameChatroom_OnDistFilesOutstanding    , CGameChatroom_OnDistFilesOutstanding);
        WriteMemory((uint32_t)&real_CGameChatroom_OnPlayerJoin              , CGameChatroom_OnPlayerJoin);
        WriteMemory((uint32_t)&real_Net_RandomNumber                        , Net_RandomNumber);
        WriteMemory((uint32_t)&real_NetLeaveGame                            , NetLeaveGame);
        WriteMemory((uint32_t)&real_CGameWar3_LoadMapSetup                  , CGameWar3_LoadMapSetup);

        // install hook
        if (DetourTransactionBegin() != NO_ERROR || DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
            session.fail("cannot start hook transaction");
        DetourAttach(&(PVOID&)real_CGlueMgr_SetGlueScreen, fake_CGlueMgr_SetGlueScreen_host);
        DetourAttach(&(PVOID&)real_CGameChatroom_OnDistFilesOutstanding, fake_CGameChatroom_OnDistFilesOutstanding);
        DetourAttach(&(PVOID&)real_CGameChatroom_OnPlayerJoin, fake_CGameChatroom_OnPlayerJoin);
        DetourAttach(&(PVOID&)real_Net_RandomNumber, fake_Net_RandomNumber);
        DetourAttach(&(PVOID&)real_NetLeaveGame, fake_NetLeaveGame);
        DetourAttach(&(PVOID&)real_CGameWar3_LoadMapSetup, fake_CGameWar3_LoadMapSetup);
        if (DetourTransactionCommit() != NO_ERROR) session.fail("hook transaction failed");

    }
    else {
        // 父启动器已限时等到主机准备完成；这里仅做零等待验证。
        session.check_ready();
        // prepare hook function address
        WriteMemory((uint32_t)&real_CGlueMgr_SetGlueScreen, CGlueMgr_SetGlueScreen);

        // install hook
        if (DetourTransactionBegin() != NO_ERROR || DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
            session.fail("cannot start hook transaction");
        DetourAttach(&(PVOID&)real_CGlueMgr_SetGlueScreen, fake_CGlueMgr_SetGlueScreen_join);
        if (DetourTransactionCommit() != NO_ERROR) session.fail("hook transaction failed");
    }
    return true;
}

extern "C" __declspec(dllexport) const char* __cdecl PluginName() {
    // loader 用 PluginName + .dll 查找配置与初始化顺序，必须和文件名一致。
    return "zz_shared_quicktest";
}
extern "C" __declspec(dllexport) void __cdecl Initialize() {
    try {
        session.configure();
        if (!session.options().ini_path.empty()) {
            quicktest::install_ini_redirect(GetModuleHandleW(L"kkapi_local_plugin.dll"),
                session.options().ini_path, session.options().log_path);
            session.log("INI_REDIRECT_READY");
        }
        if (!init_game()) session.fail("unsupported or unloaded Game.dll");
        session.log("HOOKS_READY");
    } catch (const std::exception& error) {
        session.fail(error.what());
    } catch (...) {
        session.fail("unexpected initialization failure");
    }
}
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
