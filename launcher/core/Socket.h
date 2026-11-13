#pragma once

#include "ChildProcess.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace scriptroom {

/// 阻塞式 TCP 套接字（Winsock）。对象独占套接字；shutdown_both 可跨线程调用以唤醒阻塞中的读写。
class Socket {
public:
    Socket() = default;
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    Socket(Socket &&Other) noexcept;
    Socket &operator=(Socket &&Other) noexcept;
    ~Socket();

    /// 监听 127.0.0.1 上的临时端口。
    static Socket listen_loopback();

    /// 检查 0.0.0.0 上的 TCP 端口是否空闲：能绑定即空闲，随后立即释放。
    static bool port_is_free(std::uint16_t Port);

    /// 已监听套接字的本地端口。
    std::uint16_t local_port() const;

    /// 等待最多 Timeout 的新连接；超时返回 nullopt。
    std::optional<Socket> accept_for(std::chrono::milliseconds Timeout);

    /// 连接 127.0.0.1:Port（测试与客户端使用）。
    static Socket connect_loopback(std::uint16_t Port);

    /// 接收至少一个字节。Cancel 被置位时返回 Cancelled；Timeout 为负表示一直等待。
    IoStatus recv_some(void *Buffer, std::size_t Capacity, std::size_t &Count,
                       std::chrono::milliseconds Timeout, const std::atomic<bool> &Cancel);

    /// 发送全部数据；发送超时、对端关闭或取消时抛出 Error。
    void send_all(const void *Data, std::size_t Size);

    void set_nodelay();
    void set_send_timeout(std::chrono::milliseconds Timeout);

    /// 关闭双向传输，唤醒其他线程中阻塞的调用；套接字句柄仍由本对象持有直到析构。
    void shutdown_both();

    bool valid() const { return Handle != InvalidValue; }

private:
    static constexpr std::uintptr_t InvalidValue = ~static_cast<std::uintptr_t>(0);

    explicit Socket(std::uintptr_t Raw) : Handle(Raw) {}

    std::uintptr_t Handle = InvalidValue;
};

} // namespace scriptroom
