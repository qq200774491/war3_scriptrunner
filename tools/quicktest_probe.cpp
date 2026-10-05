#include "session.hpp"
#include "callback_boundary.hpp"
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
            // 用输入驱动生命周期边界，父进程可同时检查事件和进程是否存活。
            char action;
            while (std::cin.get(action)) {
                if (action == 'm') {
                    std::cout << session.options().map << '\n' << std::flush;
                } else if (action == 'f') {
                    session.report_failure("probe recoverable lobby failure");
                    std::cout << "FAILED_REPORTED\n" << std::flush;
                } else if (action == 'e') {
                    quicktest::game_callback(session, [] { throw std::runtime_error("probe callback failure"); });
                } else if (action == 's') {
                    session.game_started();
                    std::cout << "STARTED\n" << std::flush;
                } else if (action == 'c') {
                    session.close_events();
                    session.ready();
                    std::cout << "READY_CLOSED\n" << std::flush;
                }
            }
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
