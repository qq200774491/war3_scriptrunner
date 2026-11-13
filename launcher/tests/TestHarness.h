#pragma once

#include <exception>
#include <filesystem>
#include <functional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace testing {

/// 断言失败。
class Failure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// 测试被跳过（缺少本机环境时）。
class Skipped : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Case {
    std::string Name;
    std::function<void()> Body;
};

std::vector<Case> &registry();

/// 构造即登记一个测试用例。
class Registrar {
public:
    Registrar(std::string Name, std::function<void()> Body) {
        registry().push_back({std::move(Name), std::move(Body)});
    }
};

inline void check(bool Condition, std::string_view Message = "断言失败",
                  std::source_location Where = std::source_location::current()) {
    if (!Condition) {
        throw Failure(std::string(Message) + " (" + Where.file_name() + ":" + std::to_string(Where.line()) + ")");
    }
}

template <typename A, typename B>
void check_equal(const A &Actual, const B &Expected, std::source_location Where = std::source_location::current()) {
    if (!(Actual == Expected)) {
        throw Failure(std::string("值不相等 (") + Where.file_name() + ":" + std::to_string(Where.line()) + ")");
    }
}

/// 期望 Body 抛出异常，且信息包含 Needle（为空则不检查）。
inline void check_throws(const std::function<void()> &Body, std::string_view Needle = {},
                         std::source_location Where = std::source_location::current()) {
    try {
        Body();
    } catch (const std::exception &Thrown) {
        if (!Needle.empty() && std::string_view(Thrown.what()).find(Needle) == std::string_view::npos) {
            throw Failure(std::string("异常信息不含 “") + std::string(Needle) + "”：" + Thrown.what() + " (" +
                          Where.file_name() + ":" + std::to_string(Where.line()) + ")");
        }
        return;
    }
    throw Failure(std::string("期望抛出异常 (") + Where.file_name() + ":" + std::to_string(Where.line()) + ")");
}

/// 项目根目录（含 scriptrunner.dll 的目录）。
std::filesystem::path project_root();

/// 每个测试独占的临时目录，析构时删除。
class TempDirectory {
public:
    TempDirectory();
    ~TempDirectory();
    TempDirectory(const TempDirectory &) = delete;
    TempDirectory &operator=(const TempDirectory &) = delete;

    const std::filesystem::path &path() const { return Path; }

private:
    std::filesystem::path Path;
};

} // namespace testing
