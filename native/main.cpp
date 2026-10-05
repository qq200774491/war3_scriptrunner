#include "runner.hpp"
#include "wire.hpp"
#include <Windows.h>
#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
std::vector<int> read_players(const wchar_t* argument) {
    std::wistringstream input(argument);
    std::vector<int> result;
    std::set<int> unique;
    std::wstring token;
    while (std::getline(input, token, L',')) {
        std::size_t used = 0;
        const int player = std::stoi(token, &used);
        if (used != token.size() || player < 0 || player >= 24 || !unique.insert(player).second)
            throw std::runtime_error("invalid player roster");
        result.push_back(player);
    }
    if (result.empty()) throw std::runtime_error("empty player roster");
    return result;
}
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        if (argc != 4 && argc != 5) throw std::runtime_error("usage: script_host DLL SCRIPT_DIRECTORY PLAYERS_CSV [ARCHIVE_DIRECTORY]");
        const auto players = read_players(argv[3]);
        room::Runner runner(argv[1], argv[2], players, argc == 5 ? std::filesystem::path(argv[4]) : std::filesystem::path{});
        room::Packet(3).number(1).send();
        room::InputPipe input;
        room::Bytes frame;
        auto heartbeat = GetTickCount64();
        while (input.poll()) {
            while (input.next(frame)) {
                room::Reader reader(frame);
                const auto type = reader.byte();
                if (type == 0x11) { reader.finish(); runner.close_room(); return 0; }
                if (type != 0x10 && type != 0x12) throw std::runtime_error("unknown command");
                const auto sequence = reader.sequence();
                const auto player = static_cast<int>(reader.number());
                const auto event = reader.text();
                const auto payload = reader.text();
                reader.finish();
                if (type == 0x12) {
                    if (player != -1 || (event != "_msga_reply" && event != "_msbroadcast"))
                        throw std::runtime_error("invalid internal reply");
                } else if (std::find(players.begin(), players.end(), player) == players.end())
                    throw std::runtime_error("player not in room");
                if (type == 0x10 && (event.empty() || event.front() == '_')) throw std::runtime_error("reserved event");
                runner.event(player, event, payload);
                room::Packet(4).sequence(sequence).send();
            }
            runner.tick();
            if (GetTickCount64() - heartbeat >= 500) {
                room::Packet(6).send();
                heartbeat = GetTickCount64();
            }
            Sleep(5);
        }
        runner.close_room();
        return 0;
    } catch (const std::exception& error) {
        try { room::Packet(5).text(error.what()).send(); } catch (...) {}
        return 1;
    } catch (...) {
        try { room::Packet(5).text("unhandled native exception").send(); } catch (...) {}
        return 2;
    }
}
