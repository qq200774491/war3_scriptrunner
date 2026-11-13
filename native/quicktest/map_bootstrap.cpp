#include "map_bootstrap.hpp"
#include "session.hpp"
#include <detours.h>
#include <intrin.h>
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace quicktest {
namespace {
using CreateObject = std::uint32_t(__cdecl*)();
CreateObject create_timer;
CreateObject create_group;
const Session* active_session;
std::uintptr_t bootstrap_caller;
unsigned setup_depth;
unsigned deferred_objects;
constexpr std::array expected_objects{'T', 'T', 'G', 'G', 'T', 'G', 'T'};

bool protected_map(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot inspect quicktest map header");
    std::array<char, 4096> header{};
    input.read(header.data(), header.size());
    constexpr std::array signature{'M', 'P', 'Q', '\x1a', 'C', 'A', 'M', 'E'};
    const auto size = static_cast<std::size_t>(input.gcount());
    for (std::size_t offset = 0; offset + signature.size() <= size; offset += 512) {
        if (std::memcmp(header.data() + offset, signature.data(), signature.size()) == 0) return true;
    }
    return false;
}

void report_failure() noexcept {
    try { active_session->report_failure("protected map bootstrap initialization failed"); }
    catch (...) { OutputDebugStringA("protected map bootstrap initialization failed\n"); }
}

bool defer_bootstrap_object(void* caller, char kind) {
    if (setup_depth == 0 || deferred_objects == expected_objects.size() ||
        reinterpret_cast<std::uintptr_t>(caller) != bootstrap_caller) return false;
    if (kind != expected_objects[deferred_objects])
        throw std::runtime_error("unexpected protected map globals initialization order");
    ++deferred_objects;
    if (deferred_objects == expected_objects.size())
        active_session->log("PROTECTED_GLOBALS_DEFERRED count=7");
    return true;
}

std::uint32_t __cdecl create_timer_once() noexcept {
    try { if (defer_bootstrap_object(_ReturnAddress(), 'T')) return 0; }
    catch (...) { report_failure(); }
    return create_timer();
}

std::uint32_t __cdecl create_group_once() noexcept {
    try { if (defer_bootstrap_object(_ReturnAddress(), 'G')) return 0; }
    catch (...) { report_failure(); }
    return create_group();
}

std::uintptr_t verified_bootstrap_caller() {
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"kkapi.dll"));
    if (!module) throw std::runtime_error("protected map requires kkapi.dll");
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
    // 此调用点是已实测 KKWE 平台插件的初始全局变量分派，不能套用到未知版本。
    constexpr std::uintptr_t return_offset = 0xb9e7c;
    constexpr std::array<unsigned char, 6> call{0xff, 0x95, 0xe8, 0xf7, 0xff, 0xff};
    if (pe->FileHeader.TimeDateStamp != 0x6aa9fb93 || pe->OptionalHeader.SizeOfImage != 0x1da000 ||
        std::memcmp(reinterpret_cast<const void*>(module + return_offset - call.size()), call.data(), call.size()) != 0)
        throw std::runtime_error("unsupported kkapi protected map bootstrap ABI");
    return module + return_offset;
}
}

MapSetupScope::MapSetupScope() noexcept { ++setup_depth; }
MapSetupScope::~MapSetupScope() { --setup_depth; }

void install_map_bootstrap(std::uintptr_t game, const std::filesystem::path& map, const Session& session) {
    if (!protected_map(map)) return;
    bootstrap_caller = verified_bootstrap_caller();
    active_session = &session;
    create_timer = reinterpret_cast<CreateObject>(game + 0x1deab0);
    create_group = reinterpret_cast<CreateObject>(game + 0x1de080);
    // CAME 加载器会重建暴雪全局。初始 KKAPI 先分配的四个 timer 和三个 group
    // 在加入端成为多余对象，使首帧对象计数比主机多 7；这里只延后这一次分配。
    // 真正地图 VM 的初始化和游戏期间的对象创建仍交给原生函数，不改动同步校验。
    if (DetourTransactionBegin() != NO_ERROR) throw std::runtime_error("cannot start map bootstrap hook transaction");
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR ||
        DetourAttach(reinterpret_cast<PVOID*>(&create_timer), create_timer_once) != NO_ERROR ||
        DetourAttach(reinterpret_cast<PVOID*>(&create_group), create_group_once) != NO_ERROR) {
        DetourTransactionAbort();
        throw std::runtime_error("cannot attach map bootstrap hooks");
    }
    if (DetourTransactionCommit() != NO_ERROR) throw std::runtime_error("cannot commit map bootstrap hooks");
    session.log("PROTECTED_BOOTSTRAP_READY");
}
}
