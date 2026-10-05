#pragma once
#include "session.hpp"
#include <exception>

namespace quicktest {
// 供应商回调不得传播 C++ 异常；错误上报即使失败也只终止当前隔离进程。
template<class Operation>
auto game_callback(const Session& session, Operation&& operation) noexcept -> decltype(operation()) {
    try { return operation(); }
    catch (const std::exception& error) { session.fail(error.what()); }
    catch (...) { session.fail("unexpected game callback exception"); }
}
}
