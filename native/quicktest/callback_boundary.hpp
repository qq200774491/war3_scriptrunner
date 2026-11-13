#pragma once
#include "session.hpp"
#include <exception>
#include <type_traits>
#include <utility>

namespace quicktest {
namespace detail {
// 异常只在 ABI 边界转换；调用者决定是结束进程还是保留原生错误提示。
template<class Operation, class Failure>
auto invoke_callback(Operation&& operation, Failure&& failure) noexcept -> decltype(operation()) {
    try { return operation(); }
    catch (const std::exception& error) { failure(error.what()); }
    catch (...) { failure("unexpected game callback exception"); }
    if constexpr (!std::is_void_v<decltype(operation())>) return {};
}
}

template<class Operation>
auto game_callback(const Session& session, Operation&& operation) noexcept -> decltype(operation()) {
    return detail::invoke_callback(std::forward<Operation>(operation),
        [&](std::string_view message) { session.fail(message); });
}

// 地图配置或插件初始化失败只报告并返回零值，不强制关闭用户的游戏窗口。
template<class Operation>
auto recoverable_callback(const Session& session, Operation&& operation) noexcept -> decltype(operation()) {
    return detail::invoke_callback(std::forward<Operation>(operation),
        [&](std::string_view message) { session.report_failure(message); });
}
}
