// 纯逻辑测试：文本、协议帧、INI、配置、存档、全局存档、命令行引号与启动阶段。

#include "ClientArchives.h"
#include "Config.h"
#include "FileSystem.h"
#include "GlobalArchive.h"
#include "Ini.h"
#include "Launch.h"
#include "Sync.h"
#include "TestHarness.h"
#include "Text.h"
#include "Wire.h"

#include <array>
#include <thread>

namespace {

using namespace scriptroom;
using namespace testing;
namespace fs = std::filesystem;

std::span<const std::uint8_t> bytes_of(const std::string &Text) {
    return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(Text.data()), Text.size());
}

std::span<const std::uint8_t> bytes_of(const std::vector<std::uint8_t> &Data) {
    return std::span<const std::uint8_t>(Data);
}

const Registrar TextRejectsBadUtf8("text.utf8_validation", [] {
    check(is_valid_utf8("名称 plain"));
    check(!is_valid_utf8("\xC0\xAF"));          // 过长编码
    check(!is_valid_utf8("\xED\xA0\x80"));      // UTF-16 代理项
    check(!is_valid_utf8("\xF4\x90\x80\x80"));  // 超出 U+10FFFF
    check(!is_valid_utf8("\xE4\xB8"));          // 被截断
    check_equal(utf8_lossy("a\xFFz"), std::string("a\xEF\xBF\xBDz"));
    check_equal(to_lower("ABC 错误 Error"), std::string("abc 错误 error"));
});

const Registrar TextParsesNumbers("text.strict_numbers", [] {
    check_equal(parse_i64("-12").value_or(0), -12);
    check(!parse_i64("1 2").has_value());
    check(!parse_i64("").has_value());
    check(!parse_u64("-1").has_value());
    check(!parse_i64("99999999999999999999").has_value());
    check_equal(parse_double("90.5").value_or(0), 90.5);
    check(!parse_double("abc").has_value());
});

const Registrar WireRejectsInvalidFrames("wire.rejects_invalid_frames", [] {
    const std::vector<std::vector<std::uint8_t>> Frames = {
        {}, {3, 1, 0, 0}, {3, 2, 0, 0, 0}, {6, 0}, {99}};
    for (const auto &Frame : Frames) {
        check_throws([&] { wire::decode(bytes_of(Frame)); });
    }
});

const Registrar WireDecodesFrames("wire.decodes_known_frames", [] {
    const std::vector<std::uint8_t> Ack = {4, 7, 0, 0, 0, 0, 0, 0, 0};
    check_equal(std::get<wire::AckMessage>(wire::decode(bytes_of(Ack))).Sequence, std::uint64_t(7));
    const std::vector<std::uint8_t> Ready = {3, 1, 0, 0, 0};
    check(std::holds_alternative<wire::ReadyMessage>(wire::decode(bytes_of(Ready))));
    const std::vector<std::uint8_t> Log = {1, 2, 0, 0, 0, 2, 0, 0, 0, 'h', 'i'};
    const auto Message = std::get<wire::LogMessage>(wire::decode(bytes_of(Log)));
    check_equal(Message.Level, std::uint32_t(2));
    check_equal(Message.Text, std::string("hi"));
});

const Registrar WireEventPreservesPlayer("wire.event_preserves_player_and_unicode", [] {
    const std::string Bytes = wire::encode_event(0x10, 43, -1, "名称", "存档");
    check_equal(static_cast<std::uint8_t>(Bytes[0]), std::uint8_t(0x10));
    check_equal(static_cast<std::uint8_t>(Bytes[1]), std::uint8_t(43));
    for (int Index = 2; Index < 9; ++Index) {
        check_equal(static_cast<std::uint8_t>(Bytes[Index]), std::uint8_t(0));
    }
    for (int Index = 9; Index < 13; ++Index) {
        check_equal(static_cast<std::uint8_t>(Bytes[Index]), std::uint8_t(0xFF)); // -1 的小端补码
    }
    check_throws([] { wire::encode_event(0x10, 1, 0, std::string("bad\0", 4), ""); });
});

const Registrar IniRejectsAmbiguity("ini.rejects_ambiguous_documents", [] {
    for (const char *Text : {"[DzAPI]\nx=1\nX=2", "[A]\n[A]\n", "[DEFAULT]\nx=1", "[A]\nx=1\ncontinued", "[A]\n[a]"}) {
        check_throws([&] { Ini::parse_text(Text); }, {});
    }
});

const Registrar IniRoundTripsUtf16("ini.utf16_round_trip", [] {
    Ini Source = Ini::parse_text("[DzAPI]\r\nSSV-0-名称=存档值\r\nlevel=50\r\n");
    const std::string Encoded = Source.utf16();
    check_equal(static_cast<std::uint8_t>(Encoded[0]), std::uint8_t(0xFF));
    const Ini Decoded = Ini::parse(bytes_of(Encoded));
    check_equal(Decoded.get("DzAPI", "ssv-0-名称").value_or(""), std::string("存档值"));
    check_equal(Decoded.get("dzapi", "LEVEL").value_or(""), std::string("50"));
});

void write_text(const fs::path &Path, const std::string &Text) {
    atomic_write(Path, Text);
}

const Registrar ArchivesKeepOwnerData("client_archives.snapshots_take_owner_data_and_preserve_settings", [] {
    TempDirectory Directory;
    const auto Paths = prepare_client_archives(Directory.path(), std::nullopt);
    write_text(Paths[0], Ini::parse_text("[DzAPI]\nSSV-0-a=own0\nSSV-1-a=stale\nlevel=50").utf16());
    write_text(Paths[1], Ini::parse_text("[DzAPI]\nSSV-0-a=stale\nSSV-1-a=own1\nlevel=60").utf16());
    prepare_client_archives(Directory.path(), std::nullopt);
    for (std::size_t Player = 0; Player < Paths.size(); ++Player) {
        const std::string Raw = read_file(Paths[Player]);
        const Ini Document = Ini::parse(bytes_of(Raw));
        check_equal(Document.get("DzAPI", "SSV-0-a").value_or(""), std::string("own0"));
        check_equal(Document.get("DzAPI", "SSV-1-a").value_or(""), std::string("own1"));
        check_equal(Document.get("DzAPI", "level").value_or(""), std::string(Player == 0 ? "50" : "60"));
    }
    // 权威槽位缺失时拒绝启动，且不改写队友的文件。
    fs::remove(Paths[0]);
    const std::string Before = read_file(Paths[1]);
    check_throws([&] { prepare_client_archives(Directory.path(), std::nullopt); });
    check_equal(read_file(Paths[1]), Before);
});

const Registrar ArchivesLockedByLiveGame("client_archives.live_ini_lock_blocks_snapshot", [] {
    TempDirectory Directory;
    const auto Paths = prepare_client_archives(Directory.path(), std::nullopt);
    const FileLock Held = exclusive_file(Paths[1].native() + L".lock");
    check_throws([&] { prepare_client_archives(Directory.path(), std::nullopt); }, "占用");
});

const Registrar ArchivesMigrateV1("client_archives.v1_layout_is_backed_up_and_migrated", [] {
    TempDirectory Directory;
    for (int Player = 0; Player < 2; ++Player) {
        const fs::path Old = Directory.path() / std::format(L"player-{}", Player) / L"dz_w3_plugin.ini";
        write_text(Old, Ini::parse_text(std::format("[DzAPI]\nSSV-{}-k=v{}\nkeep={}", Player, Player, Player + 7)).utf16());
    }
    write_text(Directory.path() / L"layout.json", R"({"version":1,"players":[0,1]})");
    const auto Paths = prepare_client_archives(Directory.path(), std::nullopt);
    check(fs::exists(Directory.path() / L"backups" / L"v1-player-0.ini"));
    check(fs::exists(Directory.path() / L"backups" / L"v1-player-1.ini"));
    const std::string Raw = read_file(Paths[1]);
    const Ini Document = Ini::parse(bytes_of(Raw));
    check_equal(Document.get("DzAPI", "SSV-0-k").value_or(""), std::string("v0"));
    check_equal(Document.get("DzAPI", "keep").value_or(""), std::string("8"));
    check_throws([&] {
        write_text(Directory.path() / L"layout.json", R"({"version":9})");
        prepare_client_archives(Directory.path(), std::nullopt);
    }, "未知客户端存档布局");
});

const Registrar GlobalArchivePersists("global_archive.persistence_validation_and_locks", [] {
    TempDirectory Directory;
    {
        GlobalArchive Archive(Directory.path());
        check_equal(Archive.request(1, "MsGAHmSet", "k", R"({"a":"你好"})").Result["errnu"], 0);
        check_equal(Archive.request(2, "MsGAHmSet", "k", R"({"b":1})").Result["errnu"], -1);
    }
    GlobalArchive Archive(Directory.path());
    check_equal(Archive.request(3, "MsGAHGetAll", "k", "").Result["data"]["value"], Json::object({{"a", "你好"}}));
    check_equal(Archive.request(4, "MsGALockAcquire", "k", "1000").Result["errnu"], -1);
    check_equal(Archive.request(5, "MsGALockAcquire", "lock", "1000").Result["data"]["ok"], true);
    check_equal(Archive.request(6, "MsGAHGetAll", "lock", "").Result["errnu"], -1);
    check_equal(Archive.request(7, "MsGALockAcquire", "lock", "1000").Result["data"]["ok"], false);
    const GlobalReply Cast = Archive.request(8, "MsBroadcast", "key", "value");
    check_equal(Cast.EventName, std::string("_msbroadcast"));
    check_equal(Cast.Result["value"], "value");
    check_equal(Archive.request(9, "MsGAHDel", "k", R"(["a"])").Result["data"]["value"], 1);
});

const Registrar CommandQuotingKeepsTrailingSlashes("launch.command_quoting_preserves_trailing_slashes", [] {
    check_equal(windows_argument("C:\\two words\\"), std::string("\"C:\\two words\\\\\""));
    check_equal(windows_argument("simple"), std::string("simple"));
    check_equal(windows_argument("a\"b"), std::string("\"a\\\"b\""));
    check_equal(windows_argument(""), std::string("\"\""));
});

const Registrar LuaLiteralsAreByteExact("launch.lua_paths_are_byte_exact", [] {
    check_equal(lua_literal("a\\b"), std::string("\"a\\\\b\""));
    check_equal(lua_literal("中"), std::string("\"\\228\\184\\173\""));
    check_equal(lua_literal("tab\t"), std::string("\"tab\\009\""));
});

const Registrar PhaseIsSticky("launch.game_started_phase_does_not_depend_on_signal_lifetime", [] {
    LaunchPhase Phase = LaunchPhase::WaitingForLan;
    observe_phase(Phase, false);
    check(Phase == LaunchPhase::WaitingForLan);
    observe_phase(Phase, true);
    observe_phase(Phase, false);
    check(Phase == LaunchPhase::LanStarted);
});

const Registrar ConfigValidates("config.load_resolves_paths_and_rejects_unknown_fields", [] {
    TempDirectory Directory;
    const fs::path Path = Directory.path() / L"cfg.json";
    write_text(Path, "\xEF\xBB\xBF" R"({"map":"m.w3x","local_entry":"e.lua","cloud_scripts":"c","archives":"a",
        "editor":"ed","source_game":"s","game":"g","seconds":60,"startup_timeout":90.5})");
    const Config Loaded = Config::load(Path);
    check_equal(Loaded.Map, Directory.path() / L"m.w3x");
    check_equal(Loaded.Seconds, std::uint64_t(60));
    check_equal(Loaded.StartupTimeout, 90.5);

    write_text(Path, R"({"map":"m","local_entry":"e","cloud_scripts":"c","archives":"a","editor":"x","source_game":"s",
        "game":"g","seconds":60,"startup_timeout":1,"extra":1})");
    check_throws([&] { Config::load(Path); }, "未知字段");
    write_text(Path, R"({"map":"m","local_entry":"e","cloud_scripts":"c","archives":"a","editor":"x","source_game":"s",
        "game":"g","seconds":0,"startup_timeout":1})");
    check_throws([&] { Config::load(Path); }, "运行时长");
    write_text(Path, R"({"map":"m","local_entry":"e","cloud_scripts":"c","archives":"a","editor":"x","source_game":"s",
        "game":"g","seconds":5,"startup_timeout":4000})");
    check_throws([&] { Config::load(Path); }, "启动超时");

    Config Saved = Loaded;
    const fs::path Out = Directory.path() / L"saved.json";
    Saved.save(Out);
    const Config Again = Config::load(Out);
    check_equal(Again.Archives, Loaded.Archives);
    check_equal(Again.StartupTimeout, 90.5);
});

const Registrar ChannelBackpressure("sync.channel_is_bounded_and_closes", [] {
    Channel<int> Queue(2);
    check(Queue.try_send(1));
    check(Queue.try_send(2));
    check(!Queue.try_send(3));
    check_equal(Queue.recv().value_or(0), 1);
    check(Queue.try_send(3));
    Queue.close();
    check(!Queue.try_send(4));
    check_equal(Queue.recv().value_or(0), 2);
    check_equal(Queue.recv().value_or(0), 3);
    check(!Queue.recv().has_value());
});

const Registrar StopSignalWakesWaiter("sync.stop_signal_wakes_waiters", [] {
    StopSignal Stop;
    check(!Stop.wait_for(std::chrono::milliseconds(10)));
    std::thread Waiter([&] { check(Stop.wait_for(std::chrono::seconds(5))); });
    Stop.request();
    Waiter.join();
    check(Stop.is_requested());
});

} // namespace
