#pragma once

#include "ChildProcess.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace scriptroom::wire {

/// 单帧上限（含 4 字节长度以外的正文）。
constexpr std::size_t MaxFrame = 1024 * 1024;

/// 宿主 → 启动器的消息类型。
struct LogMessage {
    std::uint32_t Level = 0;
    std::string Text;
};
struct EventMessage {
    std::int32_t Player = 0;
    std::string Name;
    std::string Payload; ///< 原始字节，不保证是 UTF-8
};
struct ReadyMessage {};
struct AckMessage {
    std::uint64_t Sequence = 0;
};
struct FailureMessage {
    std::string Reason;
};
struct HeartbeatMessage {};
struct GlobalMessage {
    std::uint32_t Transaction = 0;
    std::string Operation;
    std::string Key;
    std::string Argument;
};

using Message = std::variant<LogMessage, EventMessage, ReadyMessage, AckMessage, FailureMessage,
                             HeartbeatMessage, GlobalMessage>;

/// 解码一帧正文；截断、未知类型、多余字节或非法文本都抛出 Error。
Message decode(std::span<const std::uint8_t> Bytes);

/// 读取一个带 4 字节小端长度前缀的帧。EOF、取消或长度非法时抛出 Error。
Message read_frame(PipeStream &Input);

/// 写出一个带长度前缀的帧；超时、取消或对端关闭时抛出 Error。
void write_frame(PipeStream &Output, std::string_view Body, std::chrono::milliseconds Timeout);

/// 编码启动器 → 宿主的事件帧：kind、序号、玩家、事件名与载荷。
std::string encode_event(std::uint8_t Kind, std::uint64_t Sequence, std::int32_t Player,
                         std::string_view Name, std::string_view Payload);

} // namespace scriptroom::wire
