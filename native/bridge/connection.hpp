#pragma once
#include <WinSock2.h>
#include <string>
#include <string_view>
#include <memory>

namespace bridge {
// 游戏线程非阻塞推进；所有 socket 操作仅连接本机，不在回调中阻塞等候。
class Connection final {
public:
    explicit Connection(unsigned short port);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    void join(std::string_view room, int player, std::string_view token);
    void send(std::string_view id, std::string_view event, std::string_view payload);
    const std::string* poll();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
