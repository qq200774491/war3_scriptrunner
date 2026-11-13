#include "Wire.h"

#include "Error.h"
#include "Text.h"

#include <cstring>
#include <format>

namespace scriptroom::wire {

namespace {

/// 顺序读取定长小端字段的游标；越界即抛出 Error。
class Cursor {
public:
    explicit Cursor(std::span<const std::uint8_t> Data) : Rest(Data) {}

    std::span<const std::uint8_t> take(std::size_t Count) {
        require(Count <= Rest.size(), "truncated host frame");
        const std::span<const std::uint8_t> Head = Rest.first(Count);
        Rest = Rest.subspan(Count);
        return Head;
    }

    std::uint32_t u32() {
        const auto Bytes = take(4);
        return static_cast<std::uint32_t>(Bytes[0]) | (static_cast<std::uint32_t>(Bytes[1]) << 8) |
               (static_cast<std::uint32_t>(Bytes[2]) << 16) | (static_cast<std::uint32_t>(Bytes[3]) << 24);
    }

    std::uint64_t u64() {
        const auto Bytes = take(8);
        std::uint64_t Value = 0;
        for (std::size_t Index = 0; Index < 8; ++Index) {
            Value |= static_cast<std::uint64_t>(Bytes[Index]) << (8 * Index);
        }
        return Value;
    }

    std::string blob() {
        const std::uint32_t Count = u32();
        const auto Bytes = take(Count);
        return std::string(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
    }

    /// 严格 UTF-8 文本。
    std::string text() {
        std::string Value = blob();
        require(is_valid_utf8(Value), "invalid UTF-8 in host frame");
        return Value;
    }

    bool empty() const { return Rest.empty(); }

private:
    std::span<const std::uint8_t> Rest;
};

void append_u32(std::string &Out, std::uint32_t Value) {
    for (int Shift = 0; Shift < 32; Shift += 8) {
        Out.push_back(static_cast<char>((Value >> Shift) & 0xFF));
    }
}

void append_u64(std::string &Out, std::uint64_t Value) {
    for (int Shift = 0; Shift < 64; Shift += 8) {
        Out.push_back(static_cast<char>((Value >> Shift) & 0xFF));
    }
}

/// 精确读满 Count 字节；EOF 或取消时抛出 Error。
void read_exact(PipeStream &Input, void *Destination, std::size_t Count) {
    auto *Bytes = static_cast<char *>(Destination);
    std::size_t Done = 0;
    while (Done < Count) {
        std::size_t Got = 0;
        const IoStatus Status = Input.read_some(Bytes + Done, Count - Done, Got, std::chrono::milliseconds(-1));
        require(Status != IoStatus::Eof, "unexpected end of file");
        require(Status != IoStatus::Cancelled, "host reader cancelled");
        Done += Got;
    }
}

} // namespace

Message decode(std::span<const std::uint8_t> Bytes) {
    Cursor Reader(Bytes);
    const std::uint8_t Kind = Reader.take(1)[0];
    Message Result;
    switch (Kind) {
    case 1: {
        LogMessage Log;
        Log.Level = Reader.u32();
        Log.Text = utf8_lossy(Reader.blob());
        Result = std::move(Log);
        break;
    }
    case 2: {
        EventMessage Event;
        Event.Player = static_cast<std::int32_t>(Reader.u32());
        Event.Name = Reader.text();
        Event.Payload = Reader.blob();
        Result = std::move(Event);
        break;
    }
    case 3:
        require(Reader.u32() == 1, "invalid host readiness");
        Result = ReadyMessage{};
        break;
    case 4:
        Result = AckMessage{Reader.u64()};
        break;
    case 5:
        Result = FailureMessage{Reader.text()};
        break;
    case 6:
        Result = HeartbeatMessage{};
        break;
    case 7: {
        GlobalMessage Global;
        Global.Transaction = Reader.u32();
        Global.Operation = Reader.text();
        Global.Key = Reader.text();
        Global.Argument = Reader.text();
        Result = std::move(Global);
        break;
    }
    default:
        throw Error(std::format("unknown host frame type {}", Kind));
    }
    require(Reader.empty(), "trailing host bytes");
    return Result;
}

Message read_frame(PipeStream &Input) {
    std::uint8_t Header[4];
    read_exact(Input, Header, sizeof(Header));
    const std::uint32_t Size = static_cast<std::uint32_t>(Header[0]) | (static_cast<std::uint32_t>(Header[1]) << 8) |
                               (static_cast<std::uint32_t>(Header[2]) << 16) |
                               (static_cast<std::uint32_t>(Header[3]) << 24);
    require(Size >= 1 && Size <= MaxFrame, "invalid host frame length");
    std::vector<std::uint8_t> Body(Size);
    read_exact(Input, Body.data(), Body.size());
    return decode(Body);
}

void write_frame(PipeStream &Output, std::string_view Body, std::chrono::milliseconds Timeout) {
    require(!Body.empty() && Body.size() <= MaxFrame, "invalid outgoing host frame");
    std::string Frame;
    Frame.reserve(Body.size() + 4);
    append_u32(Frame, static_cast<std::uint32_t>(Body.size()));
    Frame.append(Body);
    const IoStatus Status = Output.write_all(Frame.data(), Frame.size(), Timeout);
    require(Status != IoStatus::Timeout, "host write timed out");
    require(Status != IoStatus::Cancelled, "host write cancelled");
    require(Status != IoStatus::Eof, "host closed its input");
}

std::string encode_event(std::uint8_t Kind, std::uint64_t Sequence, std::int32_t Player,
                         std::string_view Name, std::string_view Payload) {
    std::string Bytes(1, static_cast<char>(Kind));
    append_u64(Bytes, Sequence);
    append_u32(Bytes, static_cast<std::uint32_t>(Player));
    for (const std::string_view Text : {Name, Payload}) {
        require(Text.find('\0') == std::string_view::npos && Text.size() <= MaxFrame, "invalid host text");
        append_u32(Bytes, static_cast<std::uint32_t>(Text.size()));
        Bytes.append(Text);
    }
    require(Bytes.size() <= MaxFrame, "host frame exceeds limit");
    return Bytes;
}

} // namespace scriptroom::wire
