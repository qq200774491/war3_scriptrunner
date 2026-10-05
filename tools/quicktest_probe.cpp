#include "session.hpp"
#include <iostream>
#include <stdexcept>

// 隔离验证真实 Win32 命名事件，不加载 Game.dll 或创建游戏窗口。
int wmain() {
    try {
        quicktest::Session session;
        session.configure();
        if (session.options().host) {
            session.ready();
            std::cout << "READY\n" << std::flush;
            std::cin.get();
        } else {
            session.check_ready();
            std::cout << "JOIN_READY\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
