#include "Socket.h"

#include "Error.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <format>
#include <mutex>

namespace scriptroom {

namespace {

using Millis = std::chrono::milliseconds;
constexpr Millis WaitSlice{100};

void ensure_winsock() {
    static std::once_flag Started;
    std::call_once(Started, [] {
        WSADATA Data{};
        require(WSAStartup(MAKEWORD(2, 2), &Data) == 0, "初始化 Winsock 失败");
    });
}

SOCKET as_socket(std::uintptr_t Raw) {
    return static_cast<SOCKET>(Raw);
}

std::string socket_error(int Code) {
    return system_error_text(static_cast<unsigned long>(Code));
}

sockaddr_in loopback_address(std::uint16_t Port) {
    sockaddr_in Address{};
    Address.sin_family = AF_INET;
    Address.sin_port = htons(Port);
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return Address;
}

/// 等待套接字可读，按切片醒来检查取消标志。返回 true 表示可读。
IoStatus wait_readable(SOCKET Raw, Millis Timeout, const std::atomic<bool> &Cancel) {
    const bool Forever = Timeout.count() < 0;
    const auto Deadline = std::chrono::steady_clock::now() + (Forever ? Millis(0) : Timeout);
    while (true) {
        if (Cancel.load()) {
            return IoStatus::Cancelled;
        }
        Millis Slice = WaitSlice;
        if (!Forever) {
            const auto Left = std::chrono::duration_cast<Millis>(Deadline - std::chrono::steady_clock::now());
            if (Left.count() <= 0) {
                return IoStatus::Timeout;
            }
            Slice = std::min(Left, WaitSlice);
        }
        fd_set Readers;
        FD_ZERO(&Readers);
        FD_SET(Raw, &Readers);
        timeval Wait{};
        Wait.tv_sec = static_cast<long>(Slice.count() / 1000);
        Wait.tv_usec = static_cast<long>((Slice.count() % 1000) * 1000);
        const int Ready = select(0, &Readers, nullptr, nullptr, &Wait);
        if (Ready == SOCKET_ERROR) {
            throw StorageError("等待套接字失败：" + socket_error(WSAGetLastError()));
        }
        if (Ready > 0) {
            return IoStatus::Ok;
        }
    }
}

} // namespace

Socket::Socket(Socket &&Other) noexcept : Handle(std::exchange(Other.Handle, InvalidValue)) {}

Socket &Socket::operator=(Socket &&Other) noexcept {
    if (this != &Other) {
        if (valid()) {
            closesocket(as_socket(Handle));
        }
        Handle = std::exchange(Other.Handle, InvalidValue);
    }
    return *this;
}

Socket::~Socket() {
    if (valid()) {
        closesocket(as_socket(Handle));
    }
}

Socket Socket::listen_loopback() {
    ensure_winsock();
    SOCKET Raw = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    require(Raw != INVALID_SOCKET, "创建套接字失败：" + socket_error(WSAGetLastError()));
    Socket Result(static_cast<std::uintptr_t>(Raw));
    const sockaddr_in Address = loopback_address(0);
    if (bind(Raw, reinterpret_cast<const sockaddr *>(&Address), sizeof(Address)) == SOCKET_ERROR) {
        throw Error("绑定 127.0.0.1 失败：" + socket_error(WSAGetLastError()));
    }
    if (listen(Raw, 64) == SOCKET_ERROR) {
        throw Error("监听失败：" + socket_error(WSAGetLastError()));
    }
    return Result;
}

bool Socket::port_is_free(std::uint16_t Port) {
    ensure_winsock();
    SOCKET Raw = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    require(Raw != INVALID_SOCKET, "创建套接字失败：" + socket_error(WSAGetLastError()));
    Socket Guard(static_cast<std::uintptr_t>(Raw));
    sockaddr_in Address{};
    Address.sin_family = AF_INET;
    Address.sin_port = htons(Port);
    Address.sin_addr.s_addr = htonl(INADDR_ANY);
    return bind(Raw, reinterpret_cast<const sockaddr *>(&Address), sizeof(Address)) != SOCKET_ERROR;
}

std::uint16_t Socket::local_port() const {
    sockaddr_in Address{};
    int Length = sizeof(Address);
    require(getsockname(as_socket(Handle), reinterpret_cast<sockaddr *>(&Address), &Length) == 0,
            "读取本地端口失败：" + socket_error(WSAGetLastError()));
    return ntohs(Address.sin_port);
}

std::optional<Socket> Socket::accept_for(Millis Timeout) {
    static const std::atomic<bool> Never{false};
    const IoStatus Waited = wait_readable(as_socket(Handle), Timeout, Never);
    if (Waited != IoStatus::Ok) {
        return std::nullopt;
    }
    SOCKET Accepted = accept(as_socket(Handle), nullptr, nullptr);
    if (Accepted == INVALID_SOCKET) {
        const int Code = WSAGetLastError();
        if (Code == WSAECONNRESET || Code == WSAEWOULDBLOCK) {
            return std::nullopt;
        }
        throw Error("接受连接失败：" + socket_error(Code));
    }
    return Socket(static_cast<std::uintptr_t>(Accepted));
}

Socket Socket::connect_loopback(std::uint16_t Port) {
    ensure_winsock();
    SOCKET Raw = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    require(Raw != INVALID_SOCKET, "创建套接字失败：" + socket_error(WSAGetLastError()));
    Socket Result(static_cast<std::uintptr_t>(Raw));
    const sockaddr_in Address = loopback_address(Port);
    if (connect(Raw, reinterpret_cast<const sockaddr *>(&Address), sizeof(Address)) == SOCKET_ERROR) {
        throw Error(std::format("连接 127.0.0.1:{} 失败：{}", Port, socket_error(WSAGetLastError())));
    }
    return Result;
}

IoStatus Socket::recv_some(void *Buffer, std::size_t Capacity, std::size_t &Count, Millis Timeout,
                           const std::atomic<bool> &Cancel) {
    Count = 0;
    const IoStatus Waited = wait_readable(as_socket(Handle), Timeout, Cancel);
    if (Waited != IoStatus::Ok) {
        return Waited;
    }
    const int Got = recv(as_socket(Handle), static_cast<char *>(Buffer),
                         static_cast<int>(std::min<std::size_t>(Capacity, 1u << 20)), 0);
    if (Got == 0) {
        return IoStatus::Eof;
    }
    if (Got == SOCKET_ERROR) {
        const int Code = WSAGetLastError();
        if (Code == WSAECONNRESET || Code == WSAECONNABORTED || Code == WSAESHUTDOWN ||
            Code == WSAENOTCONN) {
            return IoStatus::Eof;
        }
        throw StorageError("接收失败：" + socket_error(Code));
    }
    Count = static_cast<std::size_t>(Got);
    return IoStatus::Ok;
}

void Socket::send_all(const void *Data, std::size_t Size) {
    const char *Bytes = static_cast<const char *>(Data);
    std::size_t Sent = 0;
    while (Sent < Size) {
        const int Put = send(as_socket(Handle), Bytes + Sent,
                             static_cast<int>(std::min<std::size_t>(Size - Sent, 1u << 20)), 0);
        if (Put == SOCKET_ERROR) {
            const int Code = WSAGetLastError();
            if (Code == WSAETIMEDOUT) {
                throw Error("发送超时");
            }
            throw Error("发送失败：" + socket_error(Code));
        }
        Sent += static_cast<std::size_t>(Put);
    }
}

void Socket::set_nodelay() {
    const BOOL On = TRUE;
    setsockopt(as_socket(Handle), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&On), sizeof(On));
}

void Socket::set_send_timeout(Millis Timeout) {
    const DWORD Value = static_cast<DWORD>(Timeout.count());
    setsockopt(as_socket(Handle), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&Value), sizeof(Value));
}

void Socket::shutdown_both() {
    if (valid()) {
        shutdown(as_socket(Handle), SD_BOTH);
    }
}

} // namespace scriptroom
