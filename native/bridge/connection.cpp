#include "connection.hpp"
#include <Windows.h>
#include <array>
#include <stdexcept>

namespace bridge {
namespace {
constexpr std::size_t limit = 256 * 1024;
class Winsock final {
public:
    Winsock() { WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("WSAStartup failed"); }
    ~Winsock() { WSACleanup(); }
    Winsock(const Winsock&) = delete;
    Winsock& operator=(const Winsock&) = delete;
};
class Socket final {
public:
    Socket() : handle_(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) {
        if (handle_ == INVALID_SOCKET) throw std::runtime_error("socket failed");
    }
    ~Socket() { closesocket(handle_); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    SOCKET get() const { return handle_; }
private:
    SOCKET handle_;
};
std::string quoted(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char ch : value) {
        if (ch == 0) throw std::runtime_error("embedded NUL");
        if (ch == '"' || ch == '\\') { result += '\\'; result += ch; }
        else if (ch < 32) { result += "\\u00"; result += hex[ch >> 4]; result += hex[ch & 15]; }
        else result += ch;
    }
    return result + '"';
}
}

class Connection::Impl final {
public:
    explicit Impl(unsigned short port) {
        u_long mode = 1;
        if (ioctlsocket(socket_.get(), FIONBIO, &mode)) throw std::runtime_error("nonblocking mode failed");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(socket_.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0)
            connected_ = true;
        else if (WSAGetLastError() != WSAEWOULDBLOCK) throw std::runtime_error("connect failed");
    }
    void queue(std::string value) {
        if (value.size() > limit || outgoing_.size() + value.size() + 1 > limit * 4)
            throw std::runtime_error("outgoing queue full");
        outgoing_ += value + '\n';
    }
    const std::string* poll() {
        update_connect();
        if (!connected_) return nullptr;
        flush();
        receive();
        auto newline = incoming_.find('\n');
        if (newline == std::string::npos) {
            if (incoming_.size() >= limit) throw std::runtime_error("line too long");
            if (peer_closed_) throw std::runtime_error("room connection closed");
            return nullptr;
        }
        if (newline > limit) throw std::runtime_error("line too long");
        line_ = incoming_.substr(0, newline);
        incoming_.erase(0, newline + 1);
        return &line_;
    }
private:
    void update_connect() {
        if (connected_) return;
        if (GetTickCount64() - started_ > 5000) throw std::runtime_error("connect timed out");
        fd_set writable{}, errors{};
        FD_ZERO(&writable); FD_ZERO(&errors);
        FD_SET(socket_.get(), &writable); FD_SET(socket_.get(), &errors);
        timeval timeout{};
        if (select(0, nullptr, &writable, &errors, &timeout) == SOCKET_ERROR)
            throw std::runtime_error("connect select failed");
        if (!writable.fd_count && !errors.fd_count) return;
        int error = 0;
        int length = sizeof(error);
        if (getsockopt(socket_.get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) || error)
            throw std::runtime_error("connection refused");
        connected_ = true;
    }
    void flush() {
        if (outgoing_.empty()) return;
        const auto count = ::send(socket_.get(), outgoing_.data(), static_cast<int>(outgoing_.size()), 0);
        if (count == SOCKET_ERROR) {
            if (WSAGetLastError() != WSAEWOULDBLOCK) throw std::runtime_error("send failed");
            return;
        }
        outgoing_.erase(0, static_cast<std::size_t>(count));
    }
    void receive() {
        if (peer_closed_) return;
        std::array<char, 4096> buffer{};
        for (int attempt = 0; attempt < 16 && incoming_.size() < limit; ++attempt) {
            const auto count = recv(socket_.get(), buffer.data(), static_cast<int>(buffer.size()), 0);
            if (count == 0) { peer_closed_ = true; return; }
            if (count == SOCKET_ERROR) {
                if (WSAGetLastError() == WSAEWOULDBLOCK) return;
                throw std::runtime_error("receive failed");
            }
            incoming_.append(buffer.data(), static_cast<std::size_t>(count));
        }
    }
    Winsock winsock_;
    Socket socket_;
    bool connected_ = false;
    bool peer_closed_ = false;
    ULONGLONG started_ = GetTickCount64();
    std::string incoming_, outgoing_, line_;
};

Connection::Connection(unsigned short port) : impl_(std::make_unique<Impl>(port)) {}
Connection::~Connection() = default;
void Connection::join(std::string_view room, int player, std::string_view token) {
    impl_->queue("{\"version\":1,\"room\":" + quoted(room) + ",\"player\":" + std::to_string(player) +
                 ",\"token\":" + quoted(token) + "}");
}
void Connection::send(std::string_view id, std::string_view event, std::string_view payload) {
    impl_->queue("{\"id\":" + quoted(id) + ",\"event\":" + quoted(event) + ",\"payload\":" + quoted(payload) + "}");
}
const std::string* Connection::poll() { return impl_->poll(); }
}
