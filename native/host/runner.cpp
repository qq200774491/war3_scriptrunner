#include "runner.hpp"
#include "wire.hpp"
#include "engine_profile.hpp"
#include "local_platform.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace room {
namespace {

void verify_dll(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open DLL");
    std::vector<unsigned char> data{std::istreambuf_iterator<char>(stream), {}};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 provider failed");
    std::array<unsigned char, 32> hash{};
    const auto status = BCryptHash(algorithm, nullptr, 0, data.data(),
                                  static_cast<ULONG>(data.size()), hash.data(), static_cast<ULONG>(hash.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("SHA256 failed");
    std::ostringstream hex;
    for (auto value : hash) hex << std::hex << std::setw(2) << std::setfill('0') << unsigned(value);
    if (hex.str() != profile::sha256) throw std::runtime_error("unverified DLL SHA256: " + hex.str());
}

class Library final {
public:
    explicit Library(const std::filesystem::path& path) {
        verify_dll(path);
        handle_ = LoadLibraryExW(std::filesystem::absolute(path).c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!handle_) throw std::runtime_error("LoadLibraryEx failed: " + std::to_string(GetLastError()));
    }
    ~Library() { FreeLibrary(handle_); }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    FARPROC symbol(const char* name) const {
        auto address = GetProcAddress(handle_, name);
        if (!address) throw std::runtime_error(std::string("missing export: ") + name);
        return address;
    }
private:
    HMODULE handle_ = nullptr;
};

// 本区域局限于已验证的 MSVC x86 ABI。__fastcall 的 EDX 占位适配 __thiscall。
using Entry = void(*)();
struct Native {
    const Entry* vtable;
    int player_count;
    LocalPlatform* platform;
    bool script_error = false;
    std::uint32_t global_sequence = 0;
};

[[noreturn]] void terminate_callback(const char* reason) noexcept {
    try { Packet(5).text(reason).send(); } catch (...) {}
    ExitProcess(110);
}
template<std::size_t Index>
[[noreturn]] void __fastcall unsupported(Native*, void*) noexcept {
    try { Packet(5).text("unsupported NativeInterface slot " + std::to_string(Index)).send(); } catch (...) {}
    ExitProcess(111);
}
void __fastcall native_log(Native* self, void*, int level, const char* message) noexcept {
    try {
        if (level >= 4) self->script_error = true;
        Packet(1).number(static_cast<std::uint32_t>(level)).text(message ? message : "").send();
    } catch (...) { terminate_callback("log callback failed"); }
}
int __fastcall player_count(Native* self, void*) noexcept { return self->player_count; }
// 异常只在宿主边界转换；不能穿过原 DLL 的 C++ ABI。
template<class Operation>
auto native_call(Operation&& operation) noexcept -> decltype(operation()) {
    try { return operation(); }
    catch (const std::exception& error) { terminate_callback(error.what()); }
    catch (...) { terminate_callback("unexpected local platform error"); }
}
const char* __fastcall player_name(Native* self, void*, int player) noexcept {
    return native_call([&] { return self->platform->player_name(player); });
}
int __fastcall room_start(Native* self, void*) noexcept { return self->platform->start_time(); }
int __fastcall played_count(Native* self, void*, int player) noexcept {
    return native_call([&] { return self->platform->played_count(player); });
}
int __fastcall room_time(Native* self, void*) noexcept { return self->platform->game_time(); }
int __fastcall room_mode(Native*, void*) noexcept { return 0; }
const char* __fastcall get_archive(Native* self, void*, int player, int* length) noexcept {
    return native_call([&]() -> const char* {
        if (!length) throw std::runtime_error("null archive length output");
        const auto* value = self->platform->archive(player);
        *length = value ? static_cast<int>(value->size()) : 0;
        return value ? value->c_str() : nullptr;
    });
}
int __fastcall save_archive(Native* self, void*, int player, const char* bytes, int length) noexcept {
    return native_call([&] {
        self->platform->save_archive(player, bytes, length);
        return 0;
    });
}
int __fastcall end_player(Native* self, void*, int player, const char*) noexcept {
    return native_call([&] {
        self->platform->end(player);
        return 0;
    });
}
// 返回事务号后才由宿主异步投递 _msga_reply，避免 Lua 尚未登记回调就收到结果。
std::uint32_t global_request(Native* self, const char* operation, const char* key, const char* value = "") noexcept {
    return native_call([&] {
        if (!key || !value || self->global_sequence == UINT32_MAX)
            throw std::runtime_error("invalid global archive request");
        const auto sequence = ++self->global_sequence;
        Packet(7).number(sequence).text(operation).text(key).text(value).send();
        return sequence;
    });
}
std::uint32_t __fastcall ga_hmset(Native* self, void*, const char* key, const char* value) noexcept {
    return global_request(self, "MsGAHmSet", key, value);
}
std::uint32_t __fastcall ga_hmget(Native* self, void*, const char* key, const char* value) noexcept {
    return global_request(self, "MsGAHmGet", key, value);
}
std::uint32_t __fastcall ga_hgetall(Native* self, void*, const char* key) noexcept {
    return global_request(self, "MsGAHGetAll", key);
}
std::uint32_t __fastcall ga_hdel(Native* self, void*, const char* key, const char* value) noexcept {
    return global_request(self, "MsGAHDel", key, value);
}
std::uint32_t __fastcall ga_lock_acquire(Native* self, void*, const char* key, std::int64_t ttl) noexcept {
    return native_call([&] { return global_request(self, "MsGALockAcquire", key, std::to_string(ttl).c_str()); });
}
std::uint32_t __fastcall ga_lock_release(Native* self, void*, const char* key) noexcept {
    return global_request(self, "MsGALockRelease", key);
}
void __fastcall ga_broadcast(Native* self, void*, const char* key, const char* value) noexcept {
    global_request(self, "MsBroadcast", key, value);
}
int __fastcall send_event(Native*, void*, int player, const char* name, const char* payload) noexcept {
    try {
        if (!name || !payload) terminate_callback("null callback string");
        Packet(2).number(static_cast<std::uint32_t>(player)).text(name).text(payload).send();
        return 0;
    } catch (...) { terminate_callback("send callback failed"); }
}
template<std::size_t... Index>
std::array<Entry, sizeof...(Index)> native_table(std::index_sequence<Index...>) {
    std::array<Entry, sizeof...(Index)> result{reinterpret_cast<Entry>(&unsupported<Index>)...};
    result[profile::log_slot] = reinterpret_cast<Entry>(&native_log);
    result[profile::player_count_slot] = reinterpret_cast<Entry>(&player_count);
    result[profile::send_event_slot] = reinterpret_cast<Entry>(&send_event);
    result[profile::player_name_slot] = reinterpret_cast<Entry>(&player_name);
    result[profile::played_count_slot] = reinterpret_cast<Entry>(&played_count);
    result[profile::room_start_slot] = reinterpret_cast<Entry>(&room_start);
    result[profile::room_loaded_slot] = reinterpret_cast<Entry>(&room_start);
    result[profile::room_time_slot] = reinterpret_cast<Entry>(&room_time);
    result[profile::room_mode_slot] = reinterpret_cast<Entry>(&room_mode);
    result[profile::get_archive_slot] = reinterpret_cast<Entry>(&get_archive);
    result[profile::save_archive_slot] = reinterpret_cast<Entry>(&save_archive);
    result[profile::end_player_slot] = reinterpret_cast<Entry>(&end_player);
    result[profile::ga_hmset_slot] = reinterpret_cast<Entry>(&ga_hmset);
    result[profile::ga_hmget_slot] = reinterpret_cast<Entry>(&ga_hmget);
    result[profile::ga_hgetall_slot] = reinterpret_cast<Entry>(&ga_hgetall);
    result[profile::ga_hdel_slot] = reinterpret_cast<Entry>(&ga_hdel);
    result[profile::ga_lock_acquire_slot] = reinterpret_cast<Entry>(&ga_lock_acquire);
    result[profile::ga_lock_release_slot] = reinterpret_cast<Entry>(&ga_lock_release);
    result[profile::ga_broadcast_slot] = reinterpret_cast<Entry>(&ga_broadcast);
    return result;
}
const auto callbacks = native_table(std::make_index_sequence<96>{});
struct ForeignRunner { const Entry* vtable; };
using Create = ForeignRunner*(__cdecl*)(Native*);
using Destroy = void(__cdecl*)(ForeignRunner*);
using Load = bool(__thiscall*)(ForeignRunner*, const char*);
using Tick = void(__thiscall*)(ForeignRunner*);
using Event = void(__thiscall*)(ForeignRunner*, const char*, const char*, int);
struct RunnerDeleter {
    Destroy destroy;
    void operator()(ForeignRunner* value) const noexcept { if (value) destroy(value); }
};
}

class Runner::Impl final {
public:
    Impl(const std::filesystem::path& dll, const std::filesystem::path& scripts,
         const std::vector<int>& players, const std::filesystem::path& archives)
        : library_(dll), platform_(players, archives), native_{callbacks.data(), static_cast<int>(players.size()), &platform_},
          runner_(reinterpret_cast<Create>(library_.symbol(profile::create_export))(&native_),
                  RunnerDeleter{reinterpret_cast<Destroy>(library_.symbol(profile::destroy_export))}) {
        if (!runner_) throw std::runtime_error("CreateScriptRunner returned null");
        auto load = reinterpret_cast<Load>(runner_->vtable[profile::load_slot]);
        const auto path = std::filesystem::absolute(scripts).u8string();
        if (!load(runner_.get(), path.c_str()) || native_.script_error)
            throw std::runtime_error("main.lua load failed");
        std::string payload = "{\"players\":[";
        for (std::size_t i = 0; i < players.size(); ++i) {
            if (i) payload += ',';
            payload += std::to_string(players[i]);
        }
        payload += "]}";
        event(-1, "_roomloaded", payload);
    }
    void event(int player, const std::string& name, const std::string& payload) {
        reinterpret_cast<Event>(runner_->vtable[profile::event_slot])(runner_.get(), name.c_str(), payload.c_str(), player);
        tick();
    }
    void tick() {
        reinterpret_cast<Tick>(runner_->vtable[profile::tick_slot])(runner_.get());
        if (native_.script_error) throw std::runtime_error("Lua callback raised an error; room terminated");
    }
private:
    Library library_;
    LocalPlatform platform_;
    Native native_;
    std::unique_ptr<ForeignRunner, RunnerDeleter> runner_;
};

Runner::Runner(const std::filesystem::path& dll, const std::filesystem::path& scripts,
               const std::vector<int>& players, const std::filesystem::path& archives)
    : impl_(std::make_unique<Impl>(dll, scripts, players, archives)) {}
Runner::~Runner() = default;
void Runner::event(int player, const std::string& name, const std::string& payload) { impl_->event(player, name, payload); }
void Runner::tick() { impl_->tick(); }
void Runner::close_room() { impl_->event(-1, "_roomover", ""); }
}
