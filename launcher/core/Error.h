#pragma once

#include <stdexcept>
#include <string>

namespace scriptroom {

/// 启动器内所有可预期失败的统一异常；what() 即可直接展示给用户的中文信息。
class Error : public std::runtime_error {
public:
    explicit Error(const std::string &Message) : std::runtime_error(Message) {}
};

/// 存储或系统 I/O 失败。与校验失败区分：全局存档会把校验失败回给脚本，
/// 而存储故障必须终止房间。
class StorageError : public Error {
public:
    using Error::Error;
};

/// 条件不成立时抛出 Error（对应原实现的 ensure!）。
inline void require(bool Condition, const std::string &Message) {
    if (!Condition) {
        throw Error(Message);
    }
}

} // namespace scriptroom
