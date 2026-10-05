#pragma once
#include <Windows.h>
#include <string>

namespace quicktest {
struct Options {
    bool host = false;
    int players = 0;
    std::string player;
    std::string map;
    std::wstring room;
    std::wstring log_path;
    std::wstring ini_path;
};

// 会话拥有命名事件；主机准备检查由外部启动器限时等待，插件内不阻塞。
class Session final {
public:
    Session() = default;
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    void configure();
    const Options& options() const { return options_; }
    void ready();
    void check_ready() const;
    void close_events();
    void log(const std::string& message) const;
    [[noreturn]] void fail(const std::string& message) const;
private:
    Options options_;
    HANDLE ready_ = nullptr;
    HANDLE failed_ = nullptr;
};
}
